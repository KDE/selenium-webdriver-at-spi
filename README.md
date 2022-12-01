# GUI Testing Made Easy!

selenium-webdriver-at-spi is a [WebDriver](https://www.w3.org/TR/webdriver/) for [Appium](https://appium.io) (based on [Selenium](https://www.selenium.dev/)) using the Linux accessibility API [AT-SPI2](https://www.freedesktop.org/wiki/Accessibility/AT-SPI2/).

It effectively enables us to write selenium-style UI tests that behind the scenes manipulate the UI through the accessibility API.
This allows better blackbox testing than what QTest and other unit test frameworks provide, while also not relying on pixmap comparision like openqa.

Check out the [wiki for documentation](https://invent.kde.org/sdk/selenium-webdriver-at-spi/-/wikis/home)
