#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>

import os
import time
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy
from selenium.common.exceptions import TimeoutException
from selenium.webdriver.common.actions.action_builder import ActionBuilder
from selenium.webdriver.common.actions.interaction import (POINTER_MOUSE, POINTER_TOUCH)
from selenium.webdriver.common.actions.mouse_button import MouseButton
from selenium.webdriver.common.actions.pointer_input import PointerInput
from selenium.webdriver.common.actions.wheel_actions import WheelActions
from selenium.webdriver.support.ui import WebDriverWait

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

    def _perform_and_wait(self, element, expected_text: str, action_builder, attempts: int = 3, timeout: int = 4) -> None:
        """Perform the input action and wait until the element text changes.

        The action may have to be retried: shortly after launch the app window
        may not yet be exposed on screen, so the compositor silently drops input
        events injected at a position where there is no surface yet (KWin can't
        resolve the target window through findToplevel()). Once the window is
        exposed, re-running the action delivers it normally. This does not mask
        real regressions: a genuinely broken handler would never produce the
        expected text, so the retries would still fail.
        """
        for _ in range(attempts):
            action_builder().perform()
            try:
                WebDriverWait(self.driver, timeout).until(lambda x: element.text == expected_text)
                return
            except TimeoutException:
                pass
        raise TimeoutException(f"Expected element text {expected_text!r} after {attempts} attempts, but got {element.text!r}")

    def _touch_tap(self, x: int, y: int) -> ActionBuilder:
        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(x, y).click()
        return action

    def _touch_drag(self, x: int, y: int, dx: int, dy: int) -> ActionBuilder:
        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(x, y).pointer_down().move_by(dx, dy).pointer_up()
        return action

    def _touch_drag_to(self, x1: int, y1: int, x2: int, y2: int) -> ActionBuilder:
        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(x1, y1).pointer_down().move_to_location(x2, y2).pointer_up()
        return action

    def _touch_long_press(self, x: int, y: int) -> ActionBuilder:
        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_TOUCH, "finger"))
        action.pointer_action.move_to_location(x, y).pointer_down().pause(3).pointer_up()
        return action

    def _mouse_tap(self, x: int, y: int, button: MouseButton = MouseButton.LEFT) -> ActionBuilder:
        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(x, y).click(None, button)
        return action

    def _mouse_drag(self, x: int, y: int, dx: int, dy: int) -> ActionBuilder:
        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(x, y).pointer_down().move_by(dx, dy).pointer_up()
        return action

    def _mouse_drag_to(self, x1: int, y1: int, x2: int, y2: int) -> ActionBuilder:
        action = ActionBuilder(self.driver, mouse=PointerInput(POINTER_MOUSE, "mouse"))
        action.pointer_action.move_to_location(x1, y1).pointer_down().move_to_location(x2, y2).pointer_up()
        return action

    def test_1_touch(self) -> None:
        element = self.driver.find_element(AppiumBy.NAME, "result")

        self._perform_and_wait(element, "touchscreen", lambda: self._touch_tap(100, 100))
        self._perform_and_wait(element, "dragged", lambda: self._touch_drag(200, 200, 200, 200))
        self._perform_and_wait(element, "touchscreen longpressed", lambda: self._touch_long_press(100, 100))
        self._perform_and_wait(element, "dragged", lambda: self._touch_drag_to(200, 200, 400, 400))

    def test_2_mouse(self) -> None:
        element = self.driver.find_element(AppiumBy.NAME, "result")

        self._perform_and_wait(element, "mouse left", lambda: self._mouse_tap(100, 100))
        self._perform_and_wait(element, "dragged", lambda: self._mouse_drag(200, 200, 200, 200))
        self._perform_and_wait(element, "mouse middle", lambda: self._mouse_tap(100, 100, MouseButton.MIDDLE))
        self._perform_and_wait(element, "dragged", lambda: self._mouse_drag_to(200, 200, 400, 400))
        self._perform_and_wait(element, "mouse right", lambda: self._mouse_tap(100, 100, MouseButton.RIGHT))

    @unittest.skipIf("KDECI_BUILD" in os.environ, "Broken in CI")
    def test_3_wheel(self) -> None:
        element = self.driver.find_element(AppiumBy.NAME, "result")

        action = ActionBuilder(self.driver)
        action.wheel_action.scroll(100, 100, 0, -15)
        action.perform()
        WebDriverWait(self.driver, 4).until(lambda x: element.text == "wheel 0 180")

        action = ActionBuilder(self.driver)
        action.wheel_action.scroll(100, 100, -15, 0)
        action.perform()
        WebDriverWait(self.driver, 4).until(lambda x: element.text == "wheel 180 0")


if __name__ == '__main__':
    unittest.main()
