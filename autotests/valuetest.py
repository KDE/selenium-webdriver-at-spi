#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2021-2022 Harald Sitter <sitter@kde.org>

import os
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy
from appium.webdriver.webdriver import ExtensionBase
from appium.webdriver.webelement import WebElement


class SetValueCommand(ExtensionBase):

    def method_name(self):
        return 'set_value'

    def set_value(self, element: WebElement, value: str):
        """
        Set the value on this element in the application
        Args:
            value: The value to be set
        """
        data = {
            'id': element.id,
            'text': value,
        }
        return self.execute(data)['value']

    def add_command(self):
        return 'post', '/session/$sessionId/appium/element/$id/value'


class ValueTest(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        options = AppiumOptions()
        # The app capability may be a command line or a desktop file id.
        options.set_capability("app", f"{os.getenv('QML_EXEC')} {os.path.dirname(os.path.realpath(__file__))}/value.qml")
        # Boilerplate, always the same
        self.driver = webdriver.Remote(command_executor='http://127.0.0.1:4723', extensions=[SetValueCommand], options=options)
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
        slider = self.driver.find_element(AppiumBy.NAME, "slider")
        self.assertEqual(float(slider.get_attribute('value')), 25.0)
        self.driver.set_value(slider, 100)
        self.assertEqual(float(slider.get_attribute('value')), 100.0)


if __name__ == '__main__':
    unittest.main()
