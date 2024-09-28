#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2024 Fushan Wen <qydwhotmail@gmail.com>

import os
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.webdriver import ExtensionBase


class DBusOnlineCommand(ExtensionBase):

    def method_name(self):
        return 'is_service_online'

    def is_service_online(self, service: str, bus_type: str = 'session'):
        """
        Set the value on this element in the application
        Args:
            value: The value to be set
        """
        data = {
            'service': service,
            'bus': bus_type,
        }
        return self.execute(data)['value']

    def add_command(self):
        return 'post', '/dbus/service/online'


class DBusTest(unittest.TestCase):
    driver: webdriver.Remote

    @classmethod
    def setUpClass(cls):
        options = AppiumOptions()
        options.set_capability("app", f"{os.getenv('QML_EXEC', '/usr/bin/qml6')} {os.path.dirname(os.path.realpath(__file__))}/value.qml")
        cls.driver = webdriver.Remote(command_executor='http://127.0.0.1:4723', options=options, extensions=[DBusOnlineCommand])

    @classmethod
    def tearDownClass(cls):
        # Make sure to terminate the driver again, lest it dangles.
        cls.driver.quit()

    def test_service_online(self):
        self.assertTrue(self.driver.is_service_online('org.a11y.Bus'))
        self.assertFalse(self.driver.is_service_online('org.a11y.Buss'))


if __name__ == '__main__':
    unittest.main()
