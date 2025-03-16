#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

import base64
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions


class SimpleCalculatorTests(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        options = AppiumOptions()
        # unused actually but need one so the driver is happy
        options.set_capability("app", "Root")
        self.driver = webdriver.Remote(command_executor='http://127.0.0.1:4723', options=options)

    @classmethod
    def tearDownClass(self):
        # Make sure to terminate the driver again, lest it dangles.
        self.driver.quit()

    def test_initialize(self):
        self.driver.set_clipboard_text("asdf")
        text = self.driver.get_clipboard_text()
        self.assertEqual(text, "asdf")

        self.driver.set_clipboard_text("qwer")
        text = self.driver.get_clipboard_text()
        self.assertEqual(text, "qwer")

        base64_str = self.driver.execute_script("mobile: getClipboard")
        text = base64.b64decode(base64_str).decode('utf-8')
        self.assertEqual(text, "qwer")


if __name__ == '__main__':
    unittest.main()
