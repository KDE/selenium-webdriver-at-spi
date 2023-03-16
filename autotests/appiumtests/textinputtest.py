#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2021-2023 Harald Sitter <sitter@kde.org>

import os
import unittest
from appium import webdriver
from appium.webdriver.common.appiumby import AppiumBy

class TextInputTest(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        desired_caps = {}
        # The app capability may be a command line or a desktop file id.
        desired_caps["app"] = f"qml {os.path.dirname(os.path.realpath(__file__))}/textinput.qml"
        # Boilerplate, always the same
        self.driver = webdriver.Remote(
            command_executor='http://127.0.0.1:4723',
            desired_capabilities=desired_caps)
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
        slider = self.driver.find_element(AppiumBy.NAME, "input")
        slider.send_keys("123")
        self.assertEqual(slider.text, "123")
        slider.clear()
        self.assertEqual(slider.text, "")


if __name__ == '__main__':
    unittest.main()
