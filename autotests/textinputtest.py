#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2021-2023 Harald Sitter <sitter@kde.org>

import os
import time
import unittest
from datetime import datetime

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy
from selenium.webdriver.common.action_chains import ActionChains


class TextInputTest(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        options = AppiumOptions()
        # The app capability may be a command line or a desktop file id.
        options.set_capability("app", f"{os.getenv('QML_EXEC')} {os.path.dirname(os.path.realpath(__file__))}/textinput.qml")
        # Boilerplate, always the same
        self.driver = webdriver.Remote(command_executor='http://127.0.0.1:4723', options=options)
        # Set a timeout for waiting to find elements. If elements cannot be found
        # in time we'll get a test failure. This should be somewhat long so as to
        # not fall over when the system is under load, but also not too long that
        # the test takes forever.
        self.driver.implicitly_wait = 10

    @classmethod
    def tearDownClass(self):
        # Make sure to terminate the driver again, lest it dangles.
        self.driver.quit()

    def test_initialize(self):
        element = self.driver.find_element(AppiumBy.NAME, "input")
        element.send_keys("1;{)!#@")
        self.assertEqual(element.text, "1;{)!#@")
        element.clear()
        time.sleep(1)
        self.assertEqual(element.text, "")

        # element implicitly has focus right now, test that we can just type globally
        ActionChains(self.driver).send_keys("1;{)!#@").perform()
        self.assertEqual(element.text, "1;{)!#@")
        element.clear()
        time.sleep(1)
        self.assertEqual(element.text, "")

    def test_pause(self):
        element = self.driver.find_element(AppiumBy.NAME, "input")
        before_time = datetime.now().timestamp()

        ActionChains(self.driver).send_keys("123").pause(5).send_keys("456").perform()

        after_time = datetime.now().timestamp()
        self.assertEqual(element.text, "123456")
        self.assertGreaterEqual(after_time - before_time, 5.0)
        element.clear()


if __name__ == '__main__':
    unittest.main()
