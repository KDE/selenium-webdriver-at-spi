#!/usr/bin/env ruby
# frozen_string_literal: true

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

require 'appium_lib'
require 'minitest/autorun'

$stdout.sync = true
$stderr.sync = true

class TestKInfoCenter < Minitest::Test
  attr_reader :driver

  def setup
    app = 'org.kde.kinfocenter.desktop'
    app = 'kinfocenter --platform xcb' if ENV['TEST_WITH_XWAYLAND']
    @appium_driver = Appium::Driver.new(
      {
        'caps' => { app: app },
        'appium_lib' => {
          server_url: 'http://127.0.0.1:4723',
          wait_timeout: 10,
          wait_interval: 0.5
        }
      }, true
    )
    @driver = @appium_driver.start_driver
  end

  def teardown
    driver&.quit
  end

  def test_search
    search = driver.find_element(:name, 'Search')
    search.click
    search.send_keys('HahA')
    assert_equal(search.text, 'HahA')
  end
end
