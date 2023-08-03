#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

import os
import unittest
from appium import webdriver

class ScreenshotTest(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        desired_caps = {}
        desired_caps["app"] = f"{os.getenv('QML_EXEC')} {os.path.dirname(os.path.realpath(__file__))}/value.qml"
        self.driver = webdriver.Remote(
            command_executor='http://127.0.0.1:4723',
            desired_capabilities=desired_caps)

    @classmethod
    def tearDownClass(self):
        self.driver.quit()

    def test_initialize(self):
        self.assertIsNotNone(self.driver.get_screenshot_as_png())
        self.driver.get_screenshot_as_file("appium_artifact_{}.png".format(self.id()))
        st = os.stat("appium_artifact_{}.png".format(self.id()))
        self.assertGreater(st.st_size, 1000)


if __name__ == '__main__':
    unittest.main()
