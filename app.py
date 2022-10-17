# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2021 Harald Sitter <sitter@kde.org>

from os import access
from flask import Flask, request, Response
import uuid
import json
import time

import pyatspi
from xml.dom import minidom
import xml.etree.ElementTree as ET
from lxml import etree


from app_roles import ROLE_NAMES

def _createNode(doc, accessible, parentElement):
  # role = accessible.getRole()
  # name = ROLE_NAMES[role]
  # if not name:
  #   raise

  e = minidom.Element(accessible.getRoleName().replace(" ","_"))

  nameA = doc.createAttribute('name')
  roleA = doc.createAttribute('role')
  descA = doc.createAttribute('description')
  e.setAttributeNode(nameA)
  e.setAttributeNode(roleA)
  e.setAttributeNode(descA)
  e.setAttribute("name", accessible.name)
  e.setAttribute("role", str(int(accessible.getRole())))
  e.setAttribute("description", accessible.description)

  for i in range(0, accessible.childCount):
    _createNode(doc, accessible.getChildAtIndex(i), e)

  parentElement.appendChild(e)



def _createNode2(accessible, parentElement):
  # role = accessible.getRole()
  # name = ROLE_NAMES[role]
  # if not name:
  #   raise

  if not accessible:
    return

  roleName = accessible.getRoleName()
  e = None
  if roleName:
    e = etree.Element(roleName.replace(" ","_"))
  else:
    e = etree.Element("accessible")

  e.set("name", accessible.name)
  e.set("role", str(int(accessible.getRole())))
  e.set("description", accessible.description)
  path = pyatspi.getPath(accessible)
  path_strs = [str(x) for x in path] # path is a list of ints for the indexes within the parents
  e.set("path", ' '.join(path_strs))

  for i in range(0, accessible.childCount):
    _createNode2(accessible.getChildAtIndex(i), e)

  if parentElement != None:
    parentElement.append(e)
  else:
    return e



# Exposes AT-SPI as a webdriver. This is written in python because C sucks and pyatspi is a first class binding so
# we lose nothing but gain the reduced sucking of python.

# https://github.com/SeleniumHQ/selenium/wiki/JsonWireProtocol#WebElement_JSON_Object.md
# https://www.w3.org/TR/webdriver1/#dfn-delete-session
# https://github.com/microsoft/WinAppDriver/blob/master/Docs/SupportedAPIs.md

# Using flask because I know nothing about writing REST in python and it seemed the most straight-forward framework.
app = Flask(__name__)
sessions = {} # global dict of open sessions

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
      self.elements = {} # a cache to hold elements between finding and interacting with
      self.browsing_context = None

      blob = json.loads(request.data)
      print(request.data)
      # TODO the blob from ruby is much more complicated god only knows why
      desired_app = None
      if 'desiredCapabilities' in blob:
        desired_app = blob['desiredCapabilities']['app']
      else:
        desired_app = blob['capabilities']['alwaysMatch']['appium:app']

      for desktop_index in range(pyatspi.Registry.getDesktopCount()):
        desktop = pyatspi.Registry.getDesktop(desktop_index)
        for app in desktop:
          if app.name == desired_app:
            self.browsing_context = app
            break
        if self.browsing_context:
          break
      # TODO raise if no context?

@app.route('/session', methods=['GET','POST', 'DELETE'])
def session():
  # TODO keyboard handling .. currently only here for testing
  pyatspi.Registry.generateKeyboardEvent(0xffe7, None, pyatspi.KEY_RELEASE)

  if request.method == 'POST':
    # TODO:
    # https://www.w3.org/TR/webdriver1/#new-session
    # 1, 3, 4, 5, 8, 9, 11, 12, 13, 14
    session = Session()
    print(request)
    sessions[session.id] = session
    print(sessions)
    return json.dumps({'value': {'sessionId': session.id, 'capabilities': {}}}), 200, {'content-type': 'application/json'}
  elif request.method == 'GET':
    # TODO impl
    print(request)
  elif request.method == 'DELETE':
    # TODO spec review

    return json.dumps({'value':None})

@app.route('/session/<session_id>', methods=['DELETE'])
def session_delete(session_id):
  if request.method == 'DELETE':
    # TODO spec review
    return json.dumps({'value':None})

def locator(session, strategy, selector, start):
  # pyatspi.findDescendant(start, lambda x: print(x))

  result = None
  if strategy == 'xpath':
    print("-- xml")
    doc = _createNode2(start, None)
    print(etree.tostring(doc, pretty_print=True))
    for c in doc.xpath(selector):
      print(c)
      path = [int(x) for x in c.get('path').split()]
      item = session.browsing_context # path is relative to the app root, not our start item!
      for i in path:
        item = item[i]
      if c.get('name') != item.name or c.get('description') != item.description:
        return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}
      print(c)
      print(c.get('name'))
      print(pyatspi.getPath(item))
      print(item)
      result = item
      break
    print("-- xml")
  else:
    # TODO can I switch this in python +++ raise on unmapped strategy
    pred = None
    if strategy == 'accessibility id': # pyatspi strings "[ roleName | name ]"
      pred = lambda x: str(x) == selector and x.getState().contains(pyatspi.STATE_VISIBLE)
    elif strategy == 'name':
      pred = lambda x: x.name == selector and x.getState().contains(pyatspi.STATE_VISIBLE)
    elif strategy == 'description':
      pred = lambda x: x.description == selector and x.getState().contains(pyatspi.STATE_VISIBLE)
    # there are also id and accessibleId but they seem not ever set. Not sure what to make of that :shrug:
    result = pyatspi.findDescendant(start, pred)
    if not result:
      return json.dumps({'value':{'error': 'no such element'}}), 404, {'content-type': 'application/json'}

  print("making uuid")
  # TODO: make the uuid persistent somehow
  unique_id = str(uuid.uuid1())
  session.elements[unique_id] = result
  return json.dumps({'value' : {'ELEMENT' : unique_id}}), 200, {'content-type': 'application/json'}

@app.route('/session/<session_id>/element', methods=['GET','POST'])
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

  # doc = minidom.Document()
  # _createNode(doc, start, doc)
  # answer = doc.toprettyxml()
  # print(answer)
  # # root = ET.fromstring(answer)
  # doc = etree.parse(answer)
  # # x = root.find("//label[starts-with(@name, \"We are sorry\")]")
  # # print(x)

  return locator(session, strategy, selector, start)

@app.route('/session/<session_id>/element/<element_id>/click', methods=['GET','POST'])
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
  return json.dumps({'value':None}), 200, {'content-type': 'application/json'}

@app.route('/session/<session_id>/element/<element_id>/text', methods=['GET','POST'])
def session_element_text(session_id, element_id):
  # TODO: spec review

  print("!!!!!!!!!!!!!")
  print(request.url)
  print(session_id)
  print(request.args)
  print(request.data)
  session = sessions[session_id]
  element = session.elements[element_id]
  # TODO should try queryText and if that excepts use the name
  print(element.name)
  print(element.description)
  return json.dumps({'value':element.name})

@app.route('/status', methods=['GET'])
def status():
  body = {
    'value': {
      'ready': 'true',
      'message': 'There is only one state. Hooray!'
    }
  }

  return json.dumps(body), 200, {'content-type': 'application/json'}

@app.route('/session/<session_id>/element/<element_id>/attribute/<name>', methods=['GET'])
def session_element_attribute(session_id, element_id, name):
  session = sessions[session_id]
  if not session:
    return json.dumps({'value': {'error': 'no such window'}}), 404, {'content-type': 'application/json'}

  element = session.elements[element_id]
  if not element:
    return json.dumps({'value': {'error': 'no such element'}}), 404, {'content-type': 'application/json'}

  print(pyatspi.STATE_VALUE_TO_NAME)
  result = None
  for value, string in pyatspi.STATE_VALUE_TO_NAME.items():
    if string == name:
      result = element.getState().contains(value)
      break

  return json.dumps({'value' : result }), 200, {'content-type': 'application/json'}

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
  if not start: # browsing context (no longer) valid
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

  textElement = element.queryEditableText()
  # textElement.setCaretOffset(textElement.characterCount)
  textElement.insertText(-1, text, len(text))
  return json.dumps({'value' : None }), 200, {'content-type': 'application/json'}
