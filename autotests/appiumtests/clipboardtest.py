#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

import unittest
from appium import webdriver

class SimpleCalculatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        desired_caps = {}
        # unused actually but need one so the driver is happy
        desired_caps["app"] = "org.kde.kcalc.desktop"
        self.driver = webdriver.Remote(
            command_executor='http://127.0.0.1:4723',
            desired_capabilities=desired_caps)

    @classmethod
    def tearDownClass(self):
        # Make sure to terminate the driver again, lest it dangles.
        self.driver.quit()

    def test_initialize(self):
        self.driver.set_clipboard_text("asdf")
        text = self.driver.get_clipboard_text()
        self.assertEqual(text, "asdf")

if __name__ == '__main__':
    suite = unittest.TestLoader().loadTestsFromTestCase(SimpleCalculatorTests)
    unittest.TextTestRunner(verbosity=2).run(suite)
