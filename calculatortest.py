# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2016 Microsoft Corporation. All rights reserved.
# SPDX-FileCopyrightText: 2021 Harald Sitter <sitter@kde.org>

import unittest
from appium import webdriver
import time

# WARNING this can fail easily when the display field shows the same number as a button because we don't tell them apart
#   (indeed we have no real way to)
class SimpleCalculatorTests(unittest.TestCase):

    @classmethod

    def setUpClass(self):
        #set up appium
        desired_caps = {}
        desired_caps["app"] = "kcalc"
        self.driver = webdriver.Remote(
            command_executor='http://127.0.0.1:4723',
            desired_capabilities= desired_caps)
        self.driver.implicitly_wait = 10

    def setUp(self):
        self.driver.find_element_by_name("AC").click()
        time.sleep(0.10)

    @classmethod
    def tearDownClass(self):
        self.driver.quit()

    def getresults(self):
        time.sleep(1)
        displaytext = self.driver.find_element_by_accessibility_id("KCalcDisplay").text
        return displaytext


    def test_initialize(self):
        self.driver.find_element_by_name("AC").click()
        self.driver.find_element_by_name("7").click()
        self.assertEqual(self.getresults(),"7")
        self.driver.find_element_by_name("AC").click()
        time.sleep(1)

    def test_addition(self):
        self.driver.find_element_by_name("1").click()
        self.driver.find_element_by_name("+").click()
        self.driver.find_element_by_name("7").click()
        self.driver.find_element_by_name("=").click()
        self.assertEqual(self.getresults(),"8")

    def test_combination(self):
        self.driver.find_element_by_name("7").click()
        self.driver.find_element_by_name("×").click()
        self.driver.find_element_by_name("9").click()
        self.driver.find_element_by_name("+").click()
        self.driver.find_element_by_name("1").click()
        self.driver.find_element_by_name("=").click()
        self.driver.find_element_by_name("÷").click()
        self.driver.find_element_by_name("8").click()
        self.driver.find_element_by_name("=").click()
        self.assertEqual(self.getresults(),"8")

    def test_division(self):
        self.driver.find_element_by_name("8").click()
        self.driver.find_element_by_name("8").click()
        self.driver.find_element_by_name("÷").click()
        self.driver.find_element_by_name("1").click()
        self.driver.find_element_by_name("1").click()
        self.driver.find_element_by_name("=").click()
        self.assertEqual(self.getresults(),"8")

    def test_multiplication(self):
        self.driver.find_element_by_name("9").click()
        self.driver.find_element_by_name("×").click()
        self.driver.find_element_by_name("9").click()
        self.driver.find_element_by_name("=").click()
        self.assertEqual(self.getresults(),"81")

    def test_subtraction(self):
        self.driver.find_element_by_name("9").click()
        self.driver.find_element_by_name("−").click()
        self.driver.find_element_by_name("1").click()
        self.driver.find_element_by_name("=").click()
        self.assertEqual(self.getresults(),"8")

if __name__ == '__main__':
    suite = unittest.TestLoader().loadTestsFromTestCase(SimpleCalculatorTests)
    unittest.TextTestRunner(verbosity=2).run(suite)
