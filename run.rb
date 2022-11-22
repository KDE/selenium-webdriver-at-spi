#!/usr/bin/env ruby
# frozen_string_literal: true

# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

require 'logger'

def find_program(name)
  @atspi_paths ||= [
    '/usr/lib/at-spi2-core/', # debians
    '/usr/libexec/', # newer debians
    '/usr/lib/at-spi2/', # suses
    '/usr/libexec/at-spi2/' # newer suses
  ]

  @atspi_paths.each do |x|
    path = "#{x}/#{name}"
    return path if File.exist?(path)
  end
  raise "Could not resolve absolute path for #{name}; searched in #{@atspi_paths.join(', ')}"
end

$stdout.sync = true # force immediate flushing without internal caching
logger = Logger.new($stdout)

unless ENV.include?('CUSTOM_BUS')
  logger.info('starting dbus session')
  ENV['CUSTOM_BUS'] = '1'
  # Using system() so we can print useful debug information after the run
  # (useful to debug problems with shutdown of started processes)
  ret = system('dbus-run-session', '--', __FILE__, *ARGV)
  logger.info('dbus session ended')
  system('ps aux')
  ret ? exit : abort
end

PORT = '4723'
AT_SPI_BUS_LAUNCHER_PATH = find_program('at-spi-bus-launcher')
AT_SPI_REGISTRY_PATH = find_program('at-spi2-registryd')
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
                   'flask', 'run', '--port', PORT, '--no-reload',
                   chdir: datadir)

i = 0
begin
  require 'net/http'
  Net::HTTP.get(URI("http://localhost:#{PORT}/status"))
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

# NB: do not KILL the launcher, it only shutsdown the a11y dbus-daemon when terminated!
Process.kill('TERM', driver_pid)
Process.kill('TERM', registry_pid)
Process.kill('TERM', launcher_pid)

logger.info "run.rb exiting #{ret}"
ret ? exit : abort
