#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>

import os
import time
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy
from selenium.webdriver.common.actions.action_builder import ActionBuilder
from selenium.webdriver.common.actions.interaction import (POINTER_MOUSE, POINTER_TOUCH)
from selenium.webdriver.common.actions.mouse_button import MouseButton
from selenium.webdriver.common.actions.pointer_input import PointerInput
from selenium.webdriver.common.actions.wheel_actions import WheelActions


class PointerInputTest(unittest.TestCase):

    driver: webdriver.Remote

    @classmethod
    def setUpClass(cls) -> None:
        options = AppiumOptions()
        # The app capability may be a command line or a desktop file id.
        options.set_capability("app", f"{os.getenv('QML_EXEC')} {os.path.dirname(os.path.realpath(__file__))}/pointerinput.qml")
        options.set_capability("timeouts", {'implicit': 10000})
        # Boilerplate, always the same
        cls.driver = webdriver.Remote(command_executor='http://127.0.0.1:4723', options=options)
        time.sleep(3)  # Make sure the window is visible

    @classmethod
    def tearDownClass(cls) -> None:
        # Make sure to terminate the driver again, lest it dangles.
        cls.driver.quit()

    def test_touch(self) -> None:
        element = self.driver.find_element(AppiumBy.NAME, "result")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(100, 100).click()
        action.perform()
        self.assertEqual(element.text, "touchscreen")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(200, 200).pointer_down().move_by(200, 200).pointer_up()
        action.perform()
        self.assertEqual(element.text, "dragged")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(100, 100).pointer_down().pause(3).pointer_up()
        action.perform()
        self.assertEqual(element.text, "touchscreen longpressed")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(200, 200).pointer_down().move_to_location(400, 400).pointer_up()
        action.perform()
        self.assertEqual(element.text, "dragged")

    def test_mouse(self) -> None:
        element = self.driver.find_element(AppiumBy.NAME, "result")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(100, 100).click()
        action.perform()
        self.assertEqual(element.text, "mouse left")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(200, 200).pointer_down().move_by(200, 200).pointer_up()
        action.perform()
        self.assertEqual(element.text, "dragged")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(100, 100).click(None, MouseButton.MIDDLE)
        action.perform()
        self.assertEqual(element.text, "mouse middle")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(200, 200).pointer_down().move_to_location(400, 400).pointer_up()
        action.perform()
        self.assertEqual(element.text, "dragged")

        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(100, 100).click(None, MouseButton.RIGHT)
        action.perform()
        self.assertEqual(element.text, "mouse right")

    def test_wheel(self) -> None:
        element = self.driver.find_element(AppiumBy.NAME, "result")

        action = ActionBuilder(self.driver)
        action.wheel_action.scroll(100, 100, 0, -15)
        action.perform()
        self.assertEqual(element.text, "wheel 0 180")

        action = ActionBuilder(self.driver)
        action.wheel_action.scroll(100, 100, -15, 0)
        action.perform()
        self.assertEqual(element.text, "wheel 180 0")


if __name__ == '__main__':
    unittest.main()
