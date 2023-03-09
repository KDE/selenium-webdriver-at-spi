#!/usr/bin/env ruby
# frozen_string_literal: true

# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

require 'appium_lib'
require 'minitest/autorun'

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
    driver.quit if driver
  end

  def test_search
    search = driver.find_element(:name, 'Search')
    search.click
    search.send_keys('cpu')

    cpu = driver.find_element(:class_name, '[list item | CPU]')
    assert(cpu.displayed?)
    cpu.click

    cpu_tab = driver.find_element(:class_name, '[page tab | CPU]')
    assert(cpu_tab.displayed?)
  end
end
