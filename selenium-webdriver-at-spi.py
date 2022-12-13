# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2021-2022 Harald Sitter <sitter@kde.org>

import base64
from datetime import datetime, timedelta
from flask import Flask, request
import uuid
import json
import sys
import os
import signal
import subprocess

import pyatspi
from lxml import etree

import gi
from gi.repository import Gio
gi.require_version('Gdk', '3.0')
from gi.repository import Gdk

from app_roles import ROLE_NAMES

# Exposes AT-SPI as a webdriver. This is written in python because C sucks and pyatspi is a first class binding so
# we lose nothing but gain the reduced sucking of python.

# https://github.com/SeleniumHQ/selenium/wiki/JsonWireProtocol#WebElement_JSON_Object.md
# https://www.w3.org/TR/webdriver
# https://github.com/microsoft/WinAppDriver/blob/master/Docs/SupportedAPIs.md
# https://www.freedesktop.org/wiki/Accessibility/PyAtSpi2Example/

sys.stdout = sys.stderr
sessions = {}  # global dict of open sessions

# Using flask because I know nothing about writing REST in python and it seemed the most straight-forward framework.
app = Flask(__name__)


@app.route('/status', methods=['GET'])
def status():
    body = {
        'value': {
            'ready': 'true',
            'message': 'There is only one state. Hooray!'
        }
    }
    return json.dumps(body), 200, {'content-type': 'application/json'}


def _createNode2(accessible, parentElement):
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
    path = pyatspi.getPath(accessible)
    path_strs = [str(x) for x in path] # path is a list of ints for the indexes within the parents
    e.set("path", ' '.join(path_strs))

    states = []
    for state in accessible.getState().getStates():
        states.append(pyatspi.stateToString(state))
    e.set("states", ', '.join(states))

    for i in range(0, accessible.childCount):
        _createNode2(accessible.getChildAtIndex(i), e)

    if parentElement != None:
        parentElement.append(e)
    else:
        return e


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
        self.timeouts = {'script': 30000, 'pageLoad': 300000, 'implicit': 5000} # implicit deviates from spec, 0 is unreasonable
        self.launched = False

        blob = json.loads(request.data)
        print(request.data)
        # TODO the blob from ruby is much more complicated god only knows why
        desired_app = None
        desired_timeouts = None
        if 'desiredCapabilities' in blob:
            if 'app' in blob['desiredCapabilities']:
                desired_app = blob['desiredCapabilities']['app']
            else:
                desired_app = blob['desiredCapabilities']['appium:app']
            if 'timeouts' in blob['desiredCapabilities']:
                desired_timeouts = blob['desiredCapabilities']['timeouts']
        else:
            if 'app' in blob['capabilities']['alwaysMatch']:
                desired_app = blob['capabilities']['alwaysMatch']['app']
            else:
                desired_app = blob['capabilities']['alwaysMatch']['appium:app']
            if 'timeouts' in blob['capabilities']['alwaysMatch']:
                desired_timeouts = blob['capabilities']['alwaysMatch']['timeouts']

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
            timedelta(milliseconds=self.timeouts['implicit'])

        context = Gio.AppLaunchContext()
        context.setenv('QT_ACCESSIBILITY', '1')
        context.setenv('QT_LINUX_ACCESSIBILITY_ALWAYS_ON', '1')
        context.setenv('KIO_DISABLE_CACHE_CLEANER', '1')  # don't dangle

        def on_launched(context, info, platform_data):
            self.pid = platform_data['pid']
            print("launched " + str(self.pid))

            while datetime.now() < end_time:
                for desktop_index in range(pyatspi.Registry.getDesktopCount()):
                    desktop = pyatspi.Registry.getDesktop(desktop_index)
                    for app in desktop:
                        print('=======')
                        print(app.name)
                        print(app.description)
                        print(app.getApplication())
                        print(app.getAttributes())
                        print(app.toolkitName)
                        print(app.path)
                        print(app.role)
                        print(app.props)
                        print(app.get_process_id())
                        print(app.id)
                        if app.get_process_id() == self.pid:
                            self.browsing_context = app
                            break
                    if self.browsing_context:
                        break
                # TODO raise if no context?
                if self.browsing_context:
                    break

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
        print(self.browsing_context)

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
        session = Session()
        print(request)
        sessions[session.id] = session
        print(sessions)

        if session.browsing_context is None:
            return json.dumps({'value': {'error': 'session not created '}}), 500, {'content-type': 'application/json'}

        return json.dumps({'value': {'sessionId': session.id, 'capabilities': {"app": session.browsing_context.name}}}), 200, {'content-type': 'application/json'}
    elif request.method == 'GET':
        # TODO impl
        print(request)
    elif request.method == 'DELETE':
        # TODO spec review
        return json.dumps({'value': None})


@app.route('/session/<session_id>', methods=['DELETE'])
def session_delete(session_id):
    if request.method == 'DELETE':
        # TODO spec review
        session = sessions[session_id]
        if not session:
            return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

        session.close()
        return json.dumps({'value': None})


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
    return json.dumps({ 'value': etree.tostring(doc, pretty_print=False).decode("utf-8") }), 200, {'content-type': 'application/xml'}


# NB: custom method to get the source without json wrapper
@app.route('/session/<session_id>/sourceRaw', methods=['GET'])
def session_source_raw(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    doc = _createNode2(session.browsing_context, None)
    return etree.tostring(doc, pretty_print=True).decode("utf-8"), 200, {'content-type': 'application/xml'}


def locator(session, strategy, selector, start):
    # pyatspi.findDescendant(start, lambda x: print(x))

    end_time = datetime.now() + \
        timedelta(milliseconds=session.timeouts['implicit'])
    results = []

    while datetime.now() < end_time:
        if strategy == 'xpath':
            print("-- xml")
            doc = _createNode2(start, None)
            print(etree.tostring(doc, pretty_print=True))
            for c in doc.xpath(selector):
                print(c)
                path = [int(x) for x in c.get('path').split()]
                # path is relative to the app root, not our start item!
                item = session.browsing_context
                for i in path:
                    item = item[i]
                if c.get('name') != item.name or c.get('description') != item.description:
                    return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}
                print(c)
                print(c.get('name'))
                print(pyatspi.getPath(item))
                print(item)
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
            accessible = pyatspi.findDescendant(start, pred)
            print(accessible)
            if accessible:
                results.append(accessible)
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
    if not start:  # browsing context (no longer) valid
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

    results = locator(session, strategy, selector, start)

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
    # TODO: spec review
    print(request.url)
    print(session_id)
    print(request.args)
    print(request.data)

    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    element = session.elements[element_id]
    if not element:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

    # TODO why is this using the blob rather than the url arg what even is the blob for here
    element = session.elements[element_id]
    print(dir(element))
    action = element.queryAction()
    print(dir(action))
    for i in range(0, action.nActions):
        print(element.getRoleName())
        print(action.getName(i))
        action_name = action.getName(i)
        if action_name == 'Press':
            action.doAction(i)
            break
        if action_name == 'Toggle':
            action.doAction(i)
            break
        if action_name == 'SetFocus':
            action.doAction(i)
            # intentionally doesn't break. on it's own that's not sufficient
    return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/element/<element_id>/text', methods=['GET'])
def session_element_text(session_id, element_id):
    session = sessions[session_id]
    element = session.elements[element_id]
    try:
        textElement = element.queryText()
        return json.dumps({'value': textElement.getText()})
    except NotImplementedError:
        return json.dumps({'value': element.name})


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

    return locator(session, strategy, selector, start)


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
    print(element.queryText())
    print(element.queryText().getText(0, -1))
    print(element.queryText().getDefaultAttributes())
    print(element.queryText().characterCount)
    print(element.queryText().caretOffset)

    try:
        textElement = element.queryEditableText()
        # textElement.setCaretOffset(textElement.characterCount)
        textElement.insertText(-1, text, len(text))
        return json.dumps({'value': None}), 200, {'content-type': 'application/json'}
    except NotImplementedError:
        print(element)
        print(str(element))
        action = element.queryAction()
        print(dir(action))
        for i in range(0, action.nActions):
            if action.getName(i) == 'SetFocus':
                action.doAction(i)
                for ch in text:
                    pyatspi.Registry.generateKeyboardEvent(char_to_keyval(ch), None, pyatspi.KEY_SYM)
                break
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
    for ch in keycode:
        pyatspi.Registry.generateKeyboardEvent(char_to_keyval(ch), None, pyatspi.KEY_SYM)
    return json.dumps({'value': None}), 200, {'content-type': 'application/json'}


@app.route('/session/<session_id>/screenshot', methods=['GET'])
def session_appium_screenshot(session_id):
    session = sessions[session_id]
    if not session:
        return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

    # NB: these values incorrectly do not include the device pixel ratio, so they are off when used on a scaling display
    position_x, position_y = session.browsing_context.getChildAtIndex(0).queryComponent().getPosition(pyatspi.XY_SCREEN)
    size_width, size_height = session.browsing_context.getChildAtIndex(0).queryComponent().getSize()

    proc = subprocess.Popen(['selenium-webdriver-at-spi-screenshotter',
                             str(position_x), str(position_y), str(size_width), str(size_height)],
                            stdout=subprocess.PIPE)
    out, err = proc.communicate()

    if not out:
        return json.dumps({'value': {'error': err}}), 404, {'content-type': 'application/json'}

    output = open(out, 'rb').read()
    os.unlink(out)
    print("READING {}".format(out))

    return json.dumps({'value': base64.b64encode(output).decode('utf-8')}), 200, {'content-type': 'application/json'}


def char_to_keyval(ch):
    print("----------::::::")
    keyval = Gdk.unicode_to_keyval(ord(ch))
    # I Don't know why this doesn't work, also doesn't work with \033 as input. :((
    # https://gitlab.gnome.org/GNOME/gtk/-/blob/gtk-3-24/gdk/gdkkeyuni.c
    if ch == "\uE00C":
        keyval = 0xff1b # escape
    elif ch == "\ue03d":
        keyval = 0xffeb # left meta
    elif ch == "\ue006":
        keyval = 0xff0d # return
    elif ch == "\ue007":
        keyval = 0xff8d # enter
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
