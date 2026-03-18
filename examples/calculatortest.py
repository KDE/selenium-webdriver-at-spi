#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2016 Microsoft Corporation. All rights reserved.
# SPDX-FileCopyrightText: 2021-2022 Harald Sitter <sitter@kde.org>

import unittest
from appium import webdriver
from appium.webdriver.common.appiumby import AppiumBy
from appium.options.common.base import AppiumOptions
import selenium.common.exceptions
from selenium.webdriver.support.ui import WebDriverWait


class SimpleCalculatorTests(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        options = AppiumOptions()
        # The app capability may be a command line or a desktop file id.
        options.set_capability("app", "org.kde.kcalc.desktop")
        # Boilerplate, always the same
        self.driver = webdriver.Remote(command_executor="http://127.0.0.1:4723", options=options)
        # Set a timeout for waiting to find elements. If elements cannot be found
        # in time we'll get a test failure. This should be somewhat long so as to
        # not fall over when the system is under load, but also not too long that
        # the test takes forever.
        self.driver.implicitly_wait = 10

    @classmethod
    def tearDownClass(self):
        # Make sure to terminate the driver again, lest it dangles.
        self.driver.quit()

    def setUp(self):
        self.driver.find_element(by=AppiumBy.NAME, value="All clear").click()
        wait = WebDriverWait(self.driver, 20)
        wait.until(lambda x: self.getresults() == "0")

    def getresults(self):
        displaytext = self.driver.find_element(by="description", value="Result Display").text
        return displaytext

    def assertResult(self, expected):
        wait = WebDriverWait(self.driver, 20)
        try:
            wait.until(lambda x: self.getresults() == expected)
        except selenium.common.exceptions.TimeoutException:
            pass
        actual = self.getresults()
        self.assertEqual(actual, expected)

    def test_initialize(self):
        self.driver.find_element(by=AppiumBy.NAME, value="All clear").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Seven").click()
        self.assertResult("7")

    def test_addition(self):
        self.driver.find_element(by=AppiumBy.NAME, value="One").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Add").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Seven").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Equals").click()
        self.assertResult("8")

    def test_combination(self):
        self.driver.find_element(by=AppiumBy.NAME, value="Seven").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Multiply").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Nine").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Add").click()
        self.driver.find_element(by=AppiumBy.NAME, value="One").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Equals").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Divide").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Eight").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Equals").click()
        self.assertResult("8")

    def test_division(self):
        # Using find element by name twice risks the driver finding the
        # result display text rather than finding the button. To avoid
        # that, execute the call once and store that as a local value.
        button8 = self.driver.find_element(by=AppiumBy.NAME, value="Eight")
        button8.click()
        button8.click()
        self.driver.find_element(by=AppiumBy.NAME, value="Divide").click()
        button1 = self.driver.find_element(by=AppiumBy.NAME, value="One")
        button1.click()
        button1.click()
        self.driver.find_element(by=AppiumBy.NAME, value="Equals").click()
        self.assertResult("8")

    def test_multiplication(self):
        self.driver.find_element(by=AppiumBy.NAME, value="Nine").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Multiply").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Nine").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Equals").click()
        self.assertResult("81")

    def test_subtraction(self):
        self.driver.find_element(by=AppiumBy.NAME, value="Nine").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Subtract").click()
        self.driver.find_element(by=AppiumBy.NAME, value="One").click()
        self.driver.find_element(by=AppiumBy.NAME, value="Equals").click()
        self.assertResult("8")


if __name__ == "__main__":
    unittest.main()
