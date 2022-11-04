#!/usr/bin/env ruby
# frozen_string_literal: true

# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

require 'logger'
require 'optparse'

$stdout.sync = true # force immediate flushing without internal caching

logger = Logger.new($stdout)

unless ENV.include?('CUSTOM_BUS')
  logger.info('starting dbus session')
  ENV['CUSTOM_BUS'] = '1'
  system('dbus-run-session', '--', __FILE__, *ARGV)
  logger.info('dbus session ended')
  sleep(5)
  system('ps aux')
end

OptionParser.new do |opts|
  opts.banner = "Usage: #{$0} ARGS"
  opts.separator('')

  opts.on('--at-spi-bus-launcher PATH',
          'Path to --at-spi-bus-launcher bin to use for testing.') do |v|
    ENV['AT_SPI_BUS_LAUNCHER_PATH'] = v
  end

  opts.on('--at-spi-registryd PATH',
          'Path to registry bin to use for testing.') do |v|
    ENV['AT_SPI_REGISTRY_PATH'] = v
  end
end.parse!

# AT_SPI_BUS_LAUNCHER_PATH = ENV.fetch('AT_SPI_BUS_LAUNCHER_PATH')
# AT_SPI_REGISTRY_PATH = ENV.fetch('AT_SPI_REGISTRY_PATH')
AT_SPI_BUS_LAUNCHER_PATH = ENV.fetch('AT_SPI_BUS_LAUNCHER_PATH', '/usr/libexec/at-spi-bus-launcher')
AT_SPI_REGISTRY_PATH = ENV.fetch('AT_SPI_REGISTRY_PATH', '/usr/libexec/at-spi2-registryd')
logger.warn "Testing with #{AT_SPI_BUS_LAUNCHER_PATH} and #{AT_SPI_REGISTRY_PATH}"

# TODO move this elsewhere
logger.info 'Installing dependencies'
datadir = File.absolute_path("#{__dir__}/../share/selenium-webdriver-at-spi/")
if File.exist?("#{datadir}/requirements.txt")
  system('pip3', 'install', '-r', 'requirements.txt', chdir: datadir) || raise
  ENV['PATH'] = "#{Dir.home}/.local/bin:#{ENV.fetch('PATH')}"
end

logger.info 'Starting supporting services'
launcher_pid = spawn(AT_SPI_BUS_LAUNCHER_PATH, '--launch-immediately')
registry_pid = spawn(AT_SPI_REGISTRY_PATH)
driver_pid = spawn({ 'FLASK_ENV' => 'production', 'FLASK_APP' => 'selenium-webdriver-at-spi.py' },
                   'flask', 'run', '--port', '4723', '--no-reload',
                   chdir: datadir)

i = 0
begin
  require 'net/http'
  Net::HTTP.get(URI('http://localhost:4723/status'))
rescue => e
  i += 1
  if i < 30
    logger.info 'not up yet'
    sleep 0.5
    retry
  end
  raise e
end

logger.info 'starting test'
ret = system(ARGV.fetch(0))
logger.info 'tests done'

system('ps aux')

Process.kill('KILL', driver_pid)
Process.kill('KILL', registry_pid)
Process.kill('KILL', launcher_pid)

system('ps aux')

logger.info 'run.rb exiting'
ret ? exit : abort
