# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2021-2023 Harald Sitter <sitter@kde.org>

import base64
from datetime import datetime, timedelta
import numpy as np
import tempfile
import time
import traceback
from flask import Flask, request, jsonify
import uuid
import json
import sys
import os
import signal
import subprocess
from werkzeug.exceptions import HTTPException

import pyatspi
from lxml import etree

import gi
from gi.repository import GLib
from gi.repository import Gio
gi.require_version('Gdk', '3.0')
from gi.repository import Gdk
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk

from app_roles import ROLE_NAMES

# Exposes AT-SPI as a webdriver. This is written in python because C sucks and pyatspi is a first class binding so
# we lose nothing but gain the reduced sucking of python.

# https://github.com/SeleniumHQ/selenium/wiki/JsonWireProtocol#WebElement_JSON_Object.md
# https://www.w3.org/TR/webdriver
# https://github.com/microsoft/WinAppDriver/blob/master/Docs/SupportedAPIs.md
# https://www.freedesktop.org/wiki/Accessibility/PyAtSpi2Example/

EVENTLOOP_TIME = 0.1
EVENTLOOP_TIME_LONG = 0.5
sys.stdout = sys.stderr
sessions = {} # global dict of open sessions

# Give the GUI enough time to react. tests run on the CI won't always be responsive in the tight schedule established by at-spi2 (800ms) and run risk
# of timing out on (e.g.) click events. The second value is the timeout for app startup, we keep that the same as upstream.
pyatspi.setTimeout(4000, 15000)

# Using flask because I know nothing about writing REST in python and it seemed the most straight-forward framework.
app = Flask(__name__)

@app.errorhandler(Exception)
def unknown_error(e):
    if isinstance(e, HTTPException):
        return e

    return errorFromException(error='unknown error', exception=e), 500

@app.route('/status', methods=['GET'])
def status():
    body = {
        'value': {
            'ready': 'true',
            'message': 'There is only one state. Hooray!'
        }
    }
    return json.dumps(body), 200, {'content-type': 'application/json'}


def _createNode2(accessible, parentElement, indexInParents=[]):
    if not accessible:
        return
    # A bit of aggressive filtering to not introspect chromium and firefox and the likes when using the desktop root.
    if accessible.toolkitName != "Qt" and accessible.toolkitName != "at-spi-registry":
        return

    roleName = accessible.getRoleName()
    e = None
    if roleName:
        e = etree.Element(roleName.replace(" ", "_"))
    else:
        e = etree.Element("accessible")

    e.set("name", accessible.name)
    e.set("role", str(int(accessible.getRole())))
    e.set("description", accessible.description)
    if accessible.accessibleId != None:
        e.set("accessibility-id", accessible.accessibleId)
    # NB: pyatspi.getPath is bugged when the QObject has no QObject parent. Instead manually keep track of indexes.
    # while generating the xml.
    # path = pyatspi.getPath(accessible)
    # path is a list of ints for the indexes within the parents
    path_strs = [str(x) for x in indexInParents]
    e.set("path", ' '.join(path_strs))

    states = []
    for state in accessible.getState().getStates():
        states.append(pyatspi.stateToString(state))
    e.set("states", ', '.join(states))

    for i in range(0, accessible.childCount):
        newIndex = indexInParents.copy()
        newIndex.append(i)
        _createNode2(accessible.getChildAtIndex(i), e, newIndex)

    if parentElement != None:
        parentElement.append(e)
    else:
        return e


def errorFromMessage(error, message):
    return jsonify({'value': {'error': error, 'message': message}})


def errorFromException(error, exception):
    return jsonify({'value': {'error': error, 'message': str(exception), 'stacktrace': traceback.format_exc()}})


@app.route('/')
def index():
    return 'Servas'

# Encapsulates a Session object. Sessions are opened by the client and contain elements. A session is generally speaking
# an app.
# TODO: should we expose the root scope somehow? requires a special variant of session and moving logic from the
#   REST app functions to the Session object.


class Session:

    def __init__(self) -> None:
        self.id = str(uuid.uuid1())
        self.elements = {}  # a cache to hold elements between finding and interacting with
        self.browsing_context = None
        self.pid = -1
        # implicit deviates from spec, 0 is unreasonable
        self.timeouts = {'script': 30000, 'pageLoad': 300000, 'implicit': 5000}
        self.launched = False

        blob = json.loads(request.data)
        print(request.data)
        # TODO the blob from ruby is much more complicated god only knows why
        desired_app = None
        desired_timeouts = None
        desired_environ = None
        if 'desiredCapabilities' in blob:
            if 'app' in blob['desiredCapabilities']:
                desired_app = blob['desiredCapabilities']['app']
            else:
                desired_app = blob['desiredCapabilities']['appium:app']
            if 'timeouts' in blob['desiredCapabilities']:
                desired_timeouts = blob['desiredCapabilities']['timeouts']
            if 'appium:environ' in blob['desiredCapabilities']:
                desired_environ = blob['desiredCapabilities']['appium:environ']
        else:
            if 'app' in blob['capabilities']['alwaysMatch']:
                desired_app = blob['capabilities']['alwaysMatch']['app']
            else:
                desired_app = blob['capabilities']['alwaysMatch']['appium:app']
            if 'timeouts' in blob['capabilities']['alwaysMatch']:
                desired_timeouts = blob['capabilities']['alwaysMatch']['timeouts']
            if 'appium:environ' in blob['capabilities']['alwaysMatch']:
                desired_environ = blob['capabilities']['alwaysMatch']['appium:environ']

        if desired_timeouts:
            if 'script' in desired_timeouts:
                self.timeouts['script'] = desired_timeouts['script']
            if 'pageLoad' in desired_timeouts:
                self.timeouts['pageLoad'] = desired_timeouts['pageLoad']
            if 'implicit' in desired_timeouts:
                self.timeouts['implicit'] = desired_timeouts['implicit']

        if desired_app == 'Root':
            # NB: at the time of writing there can only be one desktop ever
            self.browsing_context = pyatspi.Registry.getDesktop(0)
            return

        self.launched = True
        end_time = datetime.now() + \
            timedelta(milliseconds=(self.timeouts['implicit'] * 2))

        context = Gio.AppLaunchContext()
        context.setenv('QT_ACCESSIBILITY', '1')
        context.setenv('QT_LINUX_ACCESSIBILITY_ALWAYS_ON', '1')
        context.setenv('KIO_DISABLE_CACHE_CLEANER', '1')  # don't dangle
        if isinstance(desired_environ, dict):
            for key, value in desired_environ.items():
                context.setenv(key, value)

        def on_launched(context, info, platform_data):
            self.pid = platform_data['pid']
            print("launched " + str(self.pid))

            while datetime.now() < end_time:
                for desktop_index in range(pyatspi.Registry.getDesktopCount()):
                    desktop = pyatspi.Registry.getDesktop(desktop_index)
                    for app in desktop:
                        try:
                            if app.get_process_id() == self.pid:
                                self.browsing_context = app
                                break
                        except gi.repository.GLib.GError:
                            print('stumbled over a broken process. ignoring...')
                            continue
                    if self.browsing_context:
                        break
                if self.browsing_context:
                    break
            if not self.browsing_context:
                raise RuntimeError('Failed to find application on a11y bus within time limit!')

        context.connect("launched", on_launched)

        if desired_app.endswith(".desktop"):
            appinfo = Gio.DesktopAppInfo.new(desired_app)
            appinfo.launch([], context)
        elif desired_app.isnumeric():
            on_launched(None, None, {'pid': int(desired_app)})
        else:
            appinfo = Gio.AppInfo.create_from_commandline(
                desired_app, None, Gio.AppInfoCreateFlags.NONE)
            appinfo.launch([], context)
        print("browsing context set to:", self.browsing_context)

    def close(self) -> None:
        if self.launched:
            try:
                os.kill(self.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass


@app.route('/session', methods=['GET', 'POST', 'DELETE'])
def session():
    if request.method == 'POST':
        # TODO:
        # https://www.w3.org/TR/webdriver1/#new-session
        # 1, 3, 4, 5, 8, 9, 11, 12, 13, 14
        print(request)
        try:
            session = Session()
        except Exception as e:
            return errorFromException(error='session not created', exception=e), 500
        sessions[session.id] = session
        print(sessions)

        if session.browsing_context is None:
            return errorFromMessage(error='session not created',
                                    message='Application was not found on the a11y bus for unknown reasons. It probably failed to register on the bus.'), 500

        return json.dumps({'value': {'sessionId': session.id, 'capabilities': {"app": session.browsing_context.name}}}), 200, {'content-type': 'application/json'}
    elif request.method == 'GET':
        # TODO impl
        print(request)
    elif request.method == 'DELETE':
        # TODO spec review
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>', methods=['DELETE'])
def session_delete(session_id):
    if request.method == 'DELETE':
        # TODO spec review
        session = sessions[session_id]
        if not session:
            return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

        session.close()
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/timeouts/implicit_wait', methods=['POST'])
def session_implicit_wait(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    ms = blob['ms']

    session.timeouts['implicit'] = ms
    return json.dumps({'value': None})


@app.route('/session/<session_id>/source', methods=['GET'])
def session_source(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    doc = _createNode2(session.browsing_context, None)
    return json.dumps({'value': etree.tostring(doc, pretty_print=False).decode("utf-8")}), 200, {'content-type': 'application/xml'}


# NB: custom method to get the source without json wrapper
@app.route('/session/<session_id>/sourceRaw', methods=['GET'])
def session_source_raw(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    doc = _createNode2(session.browsing_context, None)
    return etree.tostring(doc, pretty_print=True).decode("utf-8"), 200, {'content-type': 'application/xml'}


def locator(session, strategy, selector, start, findAll = False):
    # pyatspi.findDescendant(start, lambda x: print(x))

    end_time = datetime.now() + \
        timedelta(milliseconds=session.timeouts['implicit'])
    results = []

    while datetime.now() < end_time:
        if strategy == 'xpath':
            print("-- xml")
            doc = _createNode2(start, None)
            for c in doc.xpath(selector):
                path = [int(x) for x in c.get('path').split()]
                # path is relative to the app root, not our start item!
                item = session.browsing_context
                for i in path:
                    item = item[i]
                if c.get('name') != item.name or c.get('description') != item.description:
                    return []
                results.append(item)
            print("-- xml")
        else:
            # TODO can I switch this in python +++ raise on unmapped strategy
            pred = None
            if strategy == 'accessibility id':
                def pred(x): return x.accessibleId.endswith(selector) and (x.getState().contains(pyatspi.STATE_VISIBLE) and x.getState().contains(pyatspi.STATE_SENSITIVE))
            # pyatspi strings "[ roleName | name ]"
            elif strategy == 'class name':
                def pred(x): return str(x) == selector and (x.getState().contains(
                    pyatspi.STATE_VISIBLE) or x.getState().contains(pyatspi.STATE_SENSITIVE))
            elif strategy == 'name':
                def pred(x): return x.name == selector and (x.getState().contains(
                    pyatspi.STATE_VISIBLE) or x.getState().contains(pyatspi.STATE_SENSITIVE))
            elif strategy == 'description':
                def pred(x): return x.description == selector and (x.getState().contains(
                    pyatspi.STATE_VISIBLE) or x.getState().contains(pyatspi.STATE_SENSITIVE))
            # there are also id and accessibleId but they seem not ever set. Not sure what to make of that :shrug:
            if findAll:
                accessible = pyatspi.findAllDescendants(start, pred)
                if accessible:
                    results += accessible
            else:
                accessible = pyatspi.findDescendant(start, pred)
                if accessible:
                    results.append(accessible)
            print(accessible)
        if len(results) > 0:
            break

    return results


@app.route('/session/<session_id>/element', methods=['GET', 'POST'])
def session_element(session_id=None):
    # https://www.w3.org/TR/webdriver1/#dfn-find-element

    # TODO scope elements to session somehow when the session gets closed we can throw away the references
    print(request.url)
    print(session_id)
    print(request.args)
    print(request.data)
    session = sessions[session_id]
    blob = json.loads(request.data)

    strategy = blob['using']
    selector = blob['value']
    if not strategy or not selector:
        return json.dumps({'value': {'error': 'invalid argument'}}), 404, {'content-type': 'application/json'}

    start = session.browsing_context
    if not start: # browsing context (no longer) valid
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    results = locator(session, strategy, selector, start)

    if not results:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    result = results[0]
    unique_id = result.path.replace('/', '-')
    session.elements[unique_id] = result
    return json.dumps({'value': {'element-6066-11e4-a52e-4f735466cecf': unique_id}}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/elements', methods=['GET', 'POST'])
def session_element2(session_id=None):
    # https://www.w3.org/TR/webdriver1/#dfn-find-elements

    # TODO scope elements to session somehow when the session gets closed we can throw away the references
    print(request.url)
    print(session_id)
    print(request.args)
    print(request.data)
    session = sessions[session_id]
    blob = json.loads(request.data)

    strategy = blob['using']
    selector = blob['value']
    if not strategy or not selector:
        return json.dumps({'value': {'error': 'invalid argument'}}), 404, {'content-type': 'application/json'}

    start = session.browsing_context
    if not start:  # browsing context (no longer) valid
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    results = locator(session, strategy, selector, start, findAll = True)

    if not results:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    serializations = []
    for result in results:
        unique_id = result.path.replace('/', '-')
        print(unique_id)
        session.elements[unique_id] = result
        serializations.append({'element-6066-11e4-a52e-4f735466cecf': unique_id})

    return json.dumps({'value': serializations}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/click', methods=['GET', 'POST'])
def session_element_click(session_id, element_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    element = session.elements[element_id]
    if not element:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    action = element.queryAction()
    availableActions = {}
    for i in range(0, action.nActions):
        availableActions[action.getName(i)] = i

    keys = availableActions.keys()
    if 'SetFocus' in keys: # this is used in addition to actual actions so focus is where it would be expected after a click
        print("actioning focus")
        action.doAction(availableActions['SetFocus'])
        time.sleep(EVENTLOOP_TIME)

    try:
        if 'Press' in keys:
            print("actioning press")
            action.doAction(availableActions['Press'])
        elif 'Toggle' in keys:
            print("actioning toggle")
            action.doAction(availableActions['Toggle'])
        elif 'ShowMenu' in keys:
            print("actioning showmenu")
            action.doAction(availableActions['ShowMenu'])
    except gi.repository.GLib.GError as e:
        print(e)
        print("Ignoring! There is a chance your application is misbehaving. Could also just be a blocked eventloop though.")
    time.sleep(EVENTLOOP_TIME)

    return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/text', methods=['GET'])
def session_element_text(session_id, element_id):
    session = sessions[session_id]
    element = session.elements[element_id]
    try:
        textElement = element.queryText()
        return json.dumps({'value': textElement.getText(0, -1)}), 200, {'content-type': 'application/json'}
    except NotImplementedError:
        return json.dumps({'value': element.name}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/enabled', methods=['GET'])
def session_element_enabled(session_id, element_id):
    session = sessions[session_id]
    element = session.elements[element_id]
    return json.dumps({'value': element.getState().contains(pyatspi.STATE_ENABLED)}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/displayed', methods=['GET'])
def session_element_displayed(session_id, element_id):
    session = sessions[session_id]
    element = session.elements[element_id]
    return json.dumps({'value': element.getState().contains(pyatspi.STATE_VISIBLE) and element.getState().contains(pyatspi.STATE_SHOWING)}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/selected', methods=['GET'])
def session_element_selected(session_id, element_id):
    session = sessions[session_id]
    element = session.elements[element_id]
    return json.dumps({'value': (element.getState().contains(pyatspi.STATE_SELECTED) or element.getState().contains(pyatspi.STATE_FOCUSED))}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/attribute/<name>', methods=['GET'])
def session_element_attribute(session_id, element_id, name):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    element = session.elements[element_id]
    if not element:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    print(request.data)

    if name == "accessibility-id":
        return json.dumps({'value': element.accessibleId}), 200, {'content-type': 'application/json'}

    if name == "name":
        return json.dumps({'value': element.name}), 200, {'content-type': 'application/json'}

    if name == "value":
        elementValue = element.queryValue()
        return json.dumps({'value': elementValue.currentValue}), 200, {'content-type': 'application/json'}

    print(pyatspi.STATE_VALUE_TO_NAME)
    result = None
    for value, string in pyatspi.STATE_VALUE_TO_NAME.items():
        if string == name:
            result = element.getState().contains(value)
            break

    return json.dumps({'value': result}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/element', methods=['POST'])
def session_element_element(session_id, element_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    print(blob)
    strategy = blob['using']
    selector = blob['value']
    if not strategy or not selector:
        return json.dumps({'value': {'error': 'invalid argument'}}), 404, {'content-type': 'application/json'}

    start = session.elements[element_id]
    if not start:  # browsing context (no longer) valid
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    results = locator(session, strategy, selector, start)

    if not results:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    result = results[0]
    unique_id = result.path.replace('/', '-')
    session.elements[unique_id] = result
    return json.dumps({'value': {'element-6066-11e4-a52e-4f735466cecf': unique_id}}), 200, {'content-type': 'application/json'}

@app.route('/session/<session_id>/element/<element_id>/value', methods=['POST'])
def session_element_value(session_id, element_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    element = session.elements[element_id]
    if not element:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    text = blob['text']

    print(blob)
    print(element)

    try:
        offset = element.queryText().caretOffset
        textElement = element.queryEditableText()
        textElement.insertText(offset, text, len(text))
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}
    except NotImplementedError:
        print("element is not text type, falling back to synthesizing keyboard events")
        action = element.queryAction()
        processed = False
        for i in range(0, action.nActions):
            if action.getName(i) == 'SetFocus':
                processed = True
                action.doAction(i)
                time.sleep(EVENTLOOP_TIME) # give the focus time to apply
                generate_keyboard_event_text(text)
                break
        if not processed:
            raise RuntimeError("element's actions list didn't contain SetFocus. The element may be malformed")
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/clear', methods=['POST'])
def session_element_clear(session_id, element_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    element = session.elements[element_id]
    if not element:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    characterCount = element.queryText().characterCount
    try:
        textElement = element.queryEditableText()
        textElement.deleteText(0, characterCount)
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}
    except NotImplementedError:
        print("element is not text type, falling back to synthesizing keyboard events")
        action = element.queryAction()
        processed = False
        for i in range(0, action.nActions):
            if action.getName(i) == 'SetFocus':
                processed = True
                action.doAction(i)
                time.sleep(EVENTLOOP_TIME) # give the focus time to apply

                pseudo_text = ''
                pseudo_text += '\ue010' # end
                for _ in range(characterCount):
                    pseudo_text += '\ue003'  # backspace
                generate_keyboard_event_text(pseudo_text)
                break
        if not processed:
            raise RuntimeError("element's actions list didn't contain SetFocus. The element may be malformed")
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/appium/device/app_state', methods=['POST'])
def session_appium_device_app_state(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    appId = blob['appId']

    proc = subprocess.Popen(
        'selenium-webdriver-at-spi-appidlister', stdout=subprocess.PIPE)
    out, err = proc.communicate()

    apps = json.loads(out)
    print(apps)
    if appId in apps.values():
        return json.dumps({'value': 4}), 200, {'content-type': 'application/json'}
    # TODO: implement rest of codes
    return json.dumps({'value': 1}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/appium/device/terminate_app', methods=['POST'])
def session_appium_device_terminate_app(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    appId = blob['appId']

    proc = subprocess.Popen(
        'selenium-webdriver-at-spi-appidlister', stdout=subprocess.PIPE)
    out, err = proc.communicate()

    apps = json.loads(out)
    if appId in apps.values():
        pid = list(apps.keys())[list(apps.values()).index(appId)]
        os.kill(int(pid), signal.SIGKILL)
    return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/appium/device/press_keycode', methods=['POST'])
def session_appium_device_press_keycode(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    keycode = blob['keycode']
    # Not doing anything with these for now
    # metastate = blob['metastate']
    # flags = blob['flags']
    # FIXME needs testing
    generate_keyboard_event_text(keycode)
    return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/actions', methods=['POST'])
def session_actions(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)

    """
    The following is to support actions that use a specific element
    as the origin. Instead of passing that origin to the inputsynth,
    we instead immediately transform from the coordinate system of the
    element to the viewport one, and then we pass that to the inputsynth.
    """
    try:
        for parent_action in blob["actions"]:
            for action in parent_action["actions"]:

                if not ("origin" in action and isinstance(action["origin"], dict)):
                    continue

                element_id = next(iter(action["origin"].values()))
                action["origin"] = "viewport"
                element = session.elements[element_id]

                p, w, h = element, 0, 0
                while p.parent:
                    p = p.parent
                    try:
                        w, h = p.queryComponent().getSize()
                    except NotImplementedError:
                        break

                proc = subprocess.Popen(['selenium-webdriver-at-spi-positionofapp', str(element.get_process_id()), str(w), str(h)], stdout=subprocess.PIPE)
                out, err = proc.communicate()
                wx, wy = map(int, out.split())
                for i in range(20):
                    print('XXX', out, err, element.get_process_id(), session.pid, w, h, wx, wy)

                x, y = element.queryComponent().getPosition(pyatspi.XY_WINDOW)
                print('---------------------', x, y)
                action["x"] += x + wx
                action["y"] += y + wy

    except KeyError:
        pass

    if 'KWIN_PID' in os.environ:
        with tempfile.NamedTemporaryFile() as file:
            file.write(bytes(json.dumps(blob), "utf-8"))
            file.flush()
            subprocess.run(["selenium-webdriver-at-spi-inputsynth", file.name])
    else:
        raise RuntimeError("actions only work with nested kwin!")

    return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/appium/device/get_clipboard', methods=['POST'])
def session_appium_device_get_clipboard(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    contentType = blob['contentType']

    # NOTE: need a window because on wayland we must be the active window to manipulate the clipboard (currently anyway)
    window = Gtk.Window()
    window.set_default_size(20, 20)
    window.show()
    display = window.get_display()
    clipboard = Gtk.Clipboard.get_for_display(display, Gdk.SELECTION_CLIPBOARD)

    spin_glib_main_context()

    data = None
    if contentType == 'plaintext':
        data = clipboard.wait_for_text()
    else:
        raise 'content type not currently supported'

    window.close()

    spin_glib_main_context()

    return json.dumps({'value': base64.b64encode(data.encode('utf-8')).decode('utf-8')}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/appium/device/set_clipboard', methods=['POST'])
def session_appium_device_set_clipboard(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    contentType = blob['contentType']
    content = blob['content']

    # NOTE: need a window because on wayland we must be the active window to manipulate the clipboard (currently anyway)
    window = Gtk.Window()
    window.set_default_size(20, 20)
    display = window.get_display()
    clipboard = Gtk.Clipboard.get_for_display(display, Gdk.SELECTION_CLIPBOARD)

    if contentType == 'plaintext':
        clipboard.set_text(base64.b64decode(content).decode('utf-8'), -1)
    else:
        raise 'content type not currently supported'

    spin_glib_main_context()

    window.close()

    spin_glib_main_context()

    return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/appium/element/<element_id>/value', methods=['POST'])
def session_appium_element_value(session_id, element_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    element = session.elements[element_id]
    if not element:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    print("blob:", blob)
    print(element)
    value = blob['text']

    try:
        valueElement = element.queryValue()
        valueElement.currentValue = float(value)
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}
    except NotImplementedError:
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/screenshot', methods=['GET'])
def session_appium_screenshot(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    # NB: these values incorrectly do not include the device pixel ratio, so they are off when used on a scaling display
    # position_x, position_y = session.browsing_context.getChildAtIndex(0).queryComponent().getPosition(pyatspi.XY_SCREEN)
    # size_width, size_height = session.browsing_context.getChildAtIndex(0).queryComponent().getSize()

    proc = subprocess.Popen(['selenium-webdriver-at-spi-screenshotter',
                             str(0), str(0), str(0), str(0)],
                            stdout=subprocess.PIPE)
    out, err = proc.communicate()

    if not out:
        return json.dumps({'value': {'error': err}}), 404, {'content-type': 'application/json'}

    return json.dumps({'value': out.decode('utf-8')}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/appium/compare_images', methods=['POST'])
def session_appium_compare_images(session_id):
    """
    Reference:
    - https://github.com/appium/python-client/blob/master/appium/webdriver/extensions/images_comparison.py
    - https://github.com/appium/appium/blob/master/packages/opencv/lib/index.js
    """
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    blob = json.loads(request.data)
    mode: str = blob['mode']
    options: dict = blob['options']
    return_value: dict = {}

    import cv2 as cv  # The extension is slow, so load it on demand

    cv_image1 = cv.imdecode(np.fromstring(base64.b64decode(blob['firstImage']), np.uint8), cv.IMREAD_COLOR)
    cv_image2 = cv.imdecode(np.fromstring(base64.b64decode(blob['secondImage']), np.uint8), cv.IMREAD_COLOR)

    if mode == 'matchFeatures':
        # https://docs.opencv.org/3.0-beta/doc/py_tutorials/py_feature2d/py_matcher/py_matcher.html
        detectorName: str = options.get('detectorName', 'ORB')
        matchFunc: str = options.get('matchFunc', 'BruteForce')
        goodMatchesFactor: int = options.get('goodMatchesFactor', -1)

        if detectorName == 'AKAZE':
            detector = cv.AKAZE.create()
        elif detectorName == 'AGAST':
            detector = cv.AgastFeatureDetector.create()
        elif detectorName == 'BRISK':
            detector = cv.BRISK.create()
        elif detectorName == 'FAST':
            detector = cv.FastFeatureDetector.create()
        elif detectorName == 'GFTT':
            detector = cv.GFTTDetector.create()
        elif detectorName == 'KAZE':
            detector = cv.KAZE.create()
        elif detectorName == 'MSER':
            detector = cv.SIFT.create()
        else:
            detector = cv.ORB.create()

        if matchFunc == 'FlannBased':
            matcher = cv.FlannBasedMatcher.create()
        elif matchFunc == 'BruteForceL1':
            matcher = cv.BFMatcher.create(cv.NORM_L1, crossCheck=True)
        elif matchFunc == 'BruteForceHamming':
            matcher = cv.BFMatcher.create(cv.NORM_HAMMING, crossCheck=True)
        elif matchFunc == 'BruteForceHammingLut':
            matcher = cv.BFMatcher.create(cv.NORM_HAMMING2, crossCheck=True)
        elif matchFunc == 'BruteForceSL2':
            matcher = cv.BFMatcher.create(cv.NORM_L2, crossCheck=True)
        else:
            matcher = cv.BFMatcher.create(cv.NORM_L2, crossCheck=True)

        # Find the keypoints and descriptors
        kp1, des1 = detector.detectAndCompute(cv_image1, None)
        kp2, des2 = detector.detectAndCompute(cv_image2, None)
        matches = sorted(matcher.match(des1, des2), key=lambda m: m.distance)

        if len(matches) < 1:
            return json.dumps({'value': {'error': 'Could not find any matches between images. Double-check orientation, resolution, or use another detector or matching function.'}}), 404, {'content-type': 'application/json'}

        return_value['count'] = min(len(matches), goodMatchesFactor) if goodMatchesFactor > 0 else len(matches)
        return_value['points1'] = [kp1[m.queryIdx].pt for m in matches]
        return_value['rect1'] = calculate_matched_rect(return_value['points1'])
        return_value['points2'] = [kp2[m.trainIdx].pt for m in matches]
        return_value['rect2'] = calculate_matched_rect(return_value['points2'])

    elif mode == 'matchTemplate':
        threshold: float = options.get('threshold', 0.0)  # Exact match

        matched = cv.matchTemplate(cv_image1, cv_image2, cv.TM_SQDIFF_NORMED)
        min_val, max_val, min_loc, max_loc = cv.minMaxLoc(matched)
        print(min_val, max_val, min_loc, max_loc)

        if min_val <= threshold:
            x, y = min_loc
            return_value['rect'] = {
                'x': x,
                "y": y,
                'width': cv_image2.shape[1],
                'height': cv_image2.shape[0],
            }
        else:
            return json.dumps({'value': {'error': 'Cannot find any occurrences of the partial image in the full image.'}}), 404, {'content-type': 'application/json'}

    elif mode == 'getSimilarity':
        if cv_image1.shape != cv_image2.shape:
            return json.dumps({'value': {'error': 'Both images are expected to have the same size in order to calculate the similarity score.'}}), 404, {'content-type': 'application/json'}
        matched = cv.matchTemplate(cv_image1, cv_image2, cv.TM_SQDIFF_NORMED)
        min_val, max_val, min_loc, max_loc = cv.minMaxLoc(matched)
        print(min_val, max_val, min_loc, max_loc)
        return_value['score'] = 1.0 - min_val

    else:
        return json.dumps({'value': {'error': 'Mode is not supported'}}), 404, {'content-type': 'application/json'}

    return json.dumps({'value': return_value}), 200, {'content-type': 'application/json'}


def calculate_matched_rect(matched_points: list[tuple[int, int]]) -> dict[str, int]:
    if len(matched_points) < 2:
        return {
            'x': 0,
            "y": 0,
            'width': 0,
            'height': 0,
        }

    points_sorted_by_distance = sorted(matched_points, key=lambda pt: pt[0] * pt[0] + pt[1] * pt[1])
    first_point = points_sorted_by_distance[0]
    last_point = points_sorted_by_distance[1]

    return {
        'x': min(first_point[0], last_point[0]),
        "y": min(first_point[1], last_point[1]),
        'width': abs(first_point[0] - last_point[0]),
        'height': abs(first_point[1] - last_point[1]),
    }


def generate_keyboard_event_text(text):
    # using a nested kwin. need to synthesize keys into wayland (not supported in atspi right now)
    if 'KWIN_PID' in os.environ:
        with tempfile.NamedTemporaryFile() as fp:
            actions = []
            for ch in text:
                actions.append({'type': 'keyDown', 'value': ch})
                actions.append({'type': 'keyUp', 'value': ch})
            print({'actions': {'type': 'key', 'id': 'key', 'actions': actions}})
            fp.write(json.dumps({'actions': [{'type': 'key', 'id': 'key', 'actions': actions}]}).encode())
            fp.flush()
            subprocess.run(["selenium-webdriver-at-spi-inputsynth", fp.name])
    else:
        for ch in text:
            pyatspi.Registry.generateKeyboardEvent(char_to_keyval(ch), None, pyatspi.KEY_SYM)
            time.sleep(EVENTLOOP_TIME)

def keyval_to_keycode(keyval):
    keymap = Gdk.Keymap.get_default()
    ret, keys = keymap.get_entries_for_keyval(keyval)
    if not ret:
        raise RuntimeError("Failed to map key!")
    # FIXME layout 0 is not necessarily the current one (e.g. in the kcm we can configure multiple layouts)
    return keys[0]


def char_to_keyval(ch):
    print("----------::::::")
    keyval = Gdk.unicode_to_keyval(ord(ch))
    # I Don't know why this doesn't work, also doesn't work with \033 as input. :((
    # https://gitlab.gnome.org/GNOME/gtk/-/blob/gtk-3-24/gdk/gdkkeyuni.c
    # Other useful resources:
    # https://www.cl.cam.ac.uk/~mgk25/ucs/keysymdef.h
    if ch == "\uE00C":
        keyval = 0xff1b # escape
    elif ch == "\ue03d":
        keyval = 0xffeb # left meta
    elif ch == "\ue006":
        keyval = 0xff0d # return
    elif ch == "\ue007":
        keyval = 0xff8d # enter
    elif ch == "\ue003":
        keyval = 0xff08 # backspace
    elif ch == "\ue010":
        keyval = 0xff57 # end
    elif ch == "\ue012":
        keyval = 0xff51 # left
    elif ch == "\ue014":
        keyval = 0xff53 # right
    elif ch == "\ue013":
        keyval = 0xff52 # up
    elif ch == "\ue015":
        keyval = 0xff54 # down
    elif ch == "\ue004":
        keyval = 0xff09 # tab
    print(ord(ch))
    print(hex(keyval))
    return keyval


def spin_glib_main_context(repeat: int = 4):
    context = GLib.MainContext.default()
    for _ in range(repeat):
        time.sleep(EVENTLOOP_TIME_LONG)
        while context.pending():
            context.iteration(may_block=False)
