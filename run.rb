#!/usr/bin/env ruby
# frozen_string_literal: true

# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

require 'logger'

def at_bus_exists?
  IO.popen(['dbus-send', '--print-reply', '--dest=org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus.ListNames'], 'r') do |io|
    io.read.include?('"org.a11y.Bus"')
  end
end

class ATSPIBus
  def initialize(logger:)
    @logger = logger
  end

  def with(&block)
    return block.yield if at_bus_exists?

    launcher_path = find_program('at-spi-bus-launcher')
    registry_path = find_program('at-spi2-registryd')
    @logger.warn "Testing with #{launcher_path} and #{registry_path}"

    launcher_pid = spawn(launcher_path, '--launch-immediately')
    registry_pid = spawn(registry_path)
    block.yield
  ensure
    # NB: do not KILL the launcher, it only shutsdown the a11y dbus-daemon when terminated!
    Process.kill('TERM', registry_pid) if launcher_pid
    Process.kill('TERM', launcher_pid) if registry_pid
  end

  private

  def find_program(name)
    @atspi_paths ||= [
      '/usr/lib/at-spi2-core/', # debians
      '/usr/libexec/', # newer debians
      '/usr/lib/at-spi2/', # suses
      '/usr/libexec/at-spi2/', # newer suses
      '/usr/lib/' # arch
    ]

    @atspi_paths.each do |x|
      path = "#{x}/#{name}"
      return path if File.exist?(path)
    end
    raise "Could not resolve absolute path for #{name}; searched in #{@atspi_paths.join(', ')}"
  end
end

class KWin
  def self.with(&block)
    return block.yield if ENV['TEST_WITH_KWIN_WAYLAND'] == "0" && !ENV['KDECI_BUILD']

    ENV['QT_QPA_PLATFORM'] = 'wayland'
    ENV['KWIN_SCREENSHOT_NO_PERMISSION_CHECKS'] = '1'
    ENV['KWIN_WAYLAND_NO_PERMISSION_CHECKS'] = '1'
    extra_args = []
    extra_args << '--virtual' if ENV['LIBGL_ALWAYS_SOFTWARE']
    kwin_pid = spawn('kwin_wayland', '--no-lockscreen', '--no-kactivities', '--no-global-shortcuts',
                     *extra_args)
    ENV['KWIN_PID'] = kwin_pid.to_s
    block.yield
  ensure
    Process.kill('TERM', kwin_pid) if kwin_pid
  end
end

class Driver
  def self.with(datadir, &block)
    driver_pid = spawn({ 'FLASK_ENV' => 'production', 'FLASK_APP' => 'selenium-webdriver-at-spi.py' },
                       'flask', 'run', '--port', PORT, '--no-reload',
                       chdir: datadir)
    block.yield
  ensure
    Process.kill('TERM', driver_pid) if driver_pid
  end
end

$stdout.sync = true # force immediate flushing without internal caching
logger = Logger.new($stdout)

unless ENV.include?('CUSTOM_BUS') # not inside a nested bus (yet)
  if ENV.fetch('USE_CUSTOM_BUS', '0').to_i > 0 || !at_bus_exists? # should we nest at all?
    logger.info('starting dbus session')
    ENV['CUSTOM_BUS'] = '1'
    # Using system() so we can print useful debug information after the run
    # (useful to debug problems with shutdown of started processes)
    pid = spawn('dbus-run-session', '--', __FILE__, *ARGV, pgroup: true)
    pgid = Process.getpgid(pid)
    Process.wait(pid)
    ret = $?
    Process.kill('-TERM', pgid)
    logger.info('dbus session ended')
    system('ps fja')
    ret.success? ? exit : abort
  else
    logger.info('using existing dbus session')
  end
end

PORT = '4723'

# TODO move this elsewhere
logger.info 'Installing dependencies'
datadir = File.absolute_path("#{__dir__}/../share/selenium-webdriver-at-spi/")
if File.exist?("#{datadir}/requirements.txt")
  system('pip3', 'install', '-r', 'requirements.txt', chdir: datadir) || raise
  ENV['PATH'] = "#{Dir.home}/.local/bin:#{ENV.fetch('PATH')}"
end

logger.info 'Starting supporting services'
ret = false
ATSPIBus.new(logger: logger).with do
  KWin.with do
    Driver.with(datadir) do
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

      logger.info "starting test #{ARGV}"
      IO.popen(ARGV, 'r', &:readlines)
      ret = $?.success?
      logger.info 'tests done'
    end
  end
end

logger.info "run.rb exiting #{ret}"
ret ? exit : abort
