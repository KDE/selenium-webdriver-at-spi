#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2023 Fushan Wen <qydwhotmail@gmail.com>

import base64
import os
import tempfile
import time
import unittest

import cv2 as cv
import numpy as np
from appium import webdriver
from appium.options.common.base import AppiumOptions


class ImageComparisonTest(unittest.TestCase):

    driver: webdriver.Remote

    @classmethod
    def setUpClass(cls) -> None:
        options = AppiumOptions()
        # The app capability may be a command line or a desktop file id.
        options.set_capability("app", f"{os.getenv('QML_EXEC', '/usr/bin/qml6')} {os.path.dirname(os.path.realpath(__file__))}/imagecomparison.qml")
        options.set_capability("timeouts", {'implicit': 30000})
        # Boilerplate, always the same
        cls.driver = webdriver.Remote(command_executor='http://127.0.0.1:4723', options=options)
        time.sleep(3)  # Make sure the window appears in the screenshot

    @classmethod
    def tearDownClass(cls) -> None:
        # Make sure to terminate the driver again, lest it dangles.
        cls.driver.quit()

    def test_matchTemplate(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            saved_image_path: str = os.path.join(temp_dir, "screenshot.png")
            self.assertTrue(self.driver.get_screenshot_as_file(saved_image_path))
            cv_first_image = cv.imread(saved_image_path, cv.IMREAD_COLOR)
            first_image = base64.b64encode(cv.imencode('.png', cv_first_image)[1].tobytes())

        cv_second_image = np.zeros((100, 100, 3), dtype=np.uint8)
        cv_second_image[:, :] = [0, 0, 255]  # Red
        second_image = base64.b64encode(cv.imencode('.png', cv_second_image)[1].tobytes())

        result = self.driver.find_image_occurrence(first_image.decode(), second_image.decode())
        self.assertEqual(result["rect"]["width"], cv_second_image.shape[1])
        self.assertEqual(result["rect"]["height"], cv_second_image.shape[0])

        cv_second_image[:, :] = [0, 255, 0]  # Green, which doesn't exist in the screenshot
        second_image = base64.b64encode(cv.imencode('.png', cv_second_image)[1].tobytes())
        self.assertRaises(Exception, self.driver.find_image_occurrence, first_image.decode(), second_image.decode())


if __name__ == '__main__':
    unittest.main()
