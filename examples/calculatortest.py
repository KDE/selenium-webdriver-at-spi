#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2016 Microsoft Corporation. All rights reserved.
# SPDX-FileCopyrightText: 2021-2022 Harald Sitter <sitter@kde.org>

import unittest
from appium import webdriver
from appium.webdriver.common.appiumby import AppiumBy
import selenium.common.exceptions
from selenium.webdriver.support.ui import WebDriverWait

class SimpleCalculatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        desired_caps = {}
        # The app capability may be a command line or a desktop file id.
        desired_caps["app"] = "org.kde.kcalc.desktop"
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

    def setUp(self):
        self.driver.find_element(by=AppiumBy.NAME, value="AC").click()
        wait = WebDriverWait(self.driver, 20)
        wait.until(lambda x: self.getresults() == '0')

    def getresults(self):
        displaytext = self.driver.find_element(by='description', value='Result Display').text
        return displaytext

    def assertResult(self, actual, expected):
        wait = WebDriverWait(self.driver, 20)
        try:
            wait.until(lambda x: self.getresults() == expected)
        except selenium.common.exceptions.TimeoutException:
            pass
        self.assertEqual(self.getresults(), expected)

    def test_initialize(self):
        self.driver.find_element(by=AppiumBy.NAME, value="AC").click()
        self.driver.find_element(by=AppiumBy.NAME, value="7").click()
        self.assertResult(self.getresults(), "7")

    def test_addition(self):
        self.driver.find_element(by=AppiumBy.NAME, value="1").click()
        self.driver.find_element(by=AppiumBy.NAME, value="+").click()
        self.driver.find_element(by=AppiumBy.NAME, value="7").click()
        self.driver.find_element(by=AppiumBy.NAME, value="=").click()
        self.assertResult(self.getresults(), "8")

    def test_combination(self):
        self.driver.find_element(by=AppiumBy.NAME, value="7").click()
        self.driver.find_element(by=AppiumBy.NAME, value="×").click()
        self.driver.find_element(by=AppiumBy.NAME, value="9").click()
        self.driver.find_element(by=AppiumBy.NAME, value="+").click()
        self.driver.find_element(by=AppiumBy.NAME, value="1").click()
        self.driver.find_element(by=AppiumBy.NAME, value="=").click()
        self.driver.find_element(by=AppiumBy.NAME, value="÷").click()
        self.driver.find_element(by=AppiumBy.NAME, value="8").click()
        self.driver.find_element(by=AppiumBy.NAME, value="=").click()
        self.assertResult(self.getresults(),"8")

    def test_division(self):
        # Using find element by name twice risks the driver finding the
        # result display text rather than finding the button. To avoid
        # that, execute the call once and store that as a local value.
        button8 = self.driver.find_element(by=AppiumBy.NAME, value="8")
        button8.click()
        button8.click()
        self.driver.find_element(by=AppiumBy.NAME, value="÷").click()
        button1 = self.driver.find_element(by=AppiumBy.NAME, value="1")
        button1.click()
        button1.click()
        self.assertResult(self.getresults(), "8")

    def test_multiplication(self):
        self.driver.find_element(by=AppiumBy.NAME, value="9").click()
        self.driver.find_element(by=AppiumBy.NAME, value="×").click()
        self.driver.find_element(by=AppiumBy.NAME, value="9").click()
        self.driver.find_element(by=AppiumBy.NAME, value="=").click()
        self.assertResult(self.getresults(), "81")

    def test_subtraction(self):
        self.driver.find_element(by=AppiumBy.NAME, value="9").click()
        self.driver.find_element(by=AppiumBy.NAME, value="−").click()
        self.driver.find_element(by=AppiumBy.NAME, value="1").click()
        self.driver.find_element(by=AppiumBy.NAME, value="=").click()
        self.assertResult(self.getresults(), "8")

if __name__ == '__main__':
    suite = unittest.TestLoader().loadTestsFromTestCase(SimpleCalculatorTests)
    unittest.TextTestRunner(verbosity=2).run(suite)
