#!/usr/bin/env ruby
# frozen_string_literal: true

# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2022-2023 Harald Sitter <sitter@kde.org>

require 'fileutils'
require 'logger'
require 'shellwords'

def at_bus_exists?
  IO.popen(['dbus-send', '--print-reply', '--dest=org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus.ListNames'], 'r') do |io|
    io.read.include?('"org.a11y.Bus"')
  end
end

def terminate_pgids(pgids)
  (pgids || []).reverse.each do |pgid|
    Process.kill('-TERM', pgid)
    Process.waitpid(pgid)
  rescue Errno::ECHILD => e
    warn "Process group not found #{e}"
  end
end

def terminate_pids(pids)
  (pids || []).reverse.each do |pid|
    Process.kill('TERM', pid)
    Process.waitpid(pid)
  end
end

class ATSPIBus
  def initialize(logger:)
    @logger = logger
  end

  def with(&block)
    return block.yield if at_bus_exists?

    bus_existed = at_bus_exists?

    launcher_path = find_program('at-spi-bus-launcher')
    registry_path = find_program('at-spi2-registryd')
    @logger.warn "Testing with #{launcher_path} and #{registry_path}"

    pids = []
    pids << spawn(launcher_path, '--launch-immediately')
    pids << spawn(registry_path)
    block.yield
  ensure
    # NB: do not signal KILL the launcher, it only shutsdown the a11y dbus-daemon when terminated!
    terminate_pids(pids)
    # Restart the regular bus or the user may be left with malfunctioning accerciser
    # (intentionally ignoring the return value! it never passes in the CI & freebsd in absence of systemd)
    system('systemctl', 'restart', '--user', 'at-spi-dbus-bus.service') if !pids&.empty? && bus_existed
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

def kwin_reexec!
  # KWin redirection is a bit tricky. We want to run this script itself under kwin so both the flask server and
  # the actual test script can inherit environment variables from that nested kwin. Most notably this is required
  # to have the correct DISPLAY set to access the xwayland instance.
  # As such this function has two behavior modes. If kwin redirection should run (that is: it's not yet inside kwin)
  # it will fork and exec into kwin. If redirection is not required it yields out.

  return if ENV.include?('KWIN_PID') # already inside a kwin parent
  return if ENV['TEST_WITH_KWIN_WAYLAND'] == '0'

  kwin_pid = fork do |pid|
    ENV['QT_QPA_PLATFORM'] = 'wayland'
    ENV['KWIN_SCREENSHOT_NO_PERMISSION_CHECKS'] = '1'
    ENV['KWIN_WAYLAND_NO_PERMISSION_CHECKS'] = '1'
    ENV['KWIN_PID'] = pid.to_s
    extra_args = []
    extra_args << '--virtual' if ENV['LIBGL_ALWAYS_SOFTWARE']
    extra_args << '--xwayland' if ENV.fetch('TEST_WITH_XWAYLAND', '0').to_i.positive?
    # A bit awkward because of how argument parsing works on the kwin side: we must rely on shell word merging for
    # the __FILE__ ARGV bit, separate ARGVs to kwin_wayland would be distinct subprocesses to start but we want
    # one processes with a bunch of arguments.
    exec('kwin_wayland', '--no-lockscreen', '--no-global-shortcuts', *extra_args,
         '--exit-with-session', "#{__FILE__} #{ARGV.shelljoin}")
  end
  _pid, status = Process.waitpid2(kwin_pid)
  status.success? ? exit : abort
end

def dbus_reexec!(logger:)
  return if ENV.include?('CUSTOM_BUS') # already inside a nested bus

  if ENV.fetch('USE_CUSTOM_BUS', '0').to_i.zero? && # not explicitly enabled
     at_bus_exists? # already have an a11y bus, use it

    logger.info('using existing dbus session')
    return
  end

  logger.info('starting dbus session')
  ENV['CUSTOM_BUS'] = '1'
  # Using spawn() rather than exec() so we can print useful debug information after the run
  # (useful to debug problems with shutdown of started processes)
  pid = spawn('dbus-run-session', '--', __FILE__, *ARGV, pgroup: true)
  pgid = Process.getpgid(pid)
  _pid, status = Process.waitpid2(pid)
  terminate_pgids([pgid])
  logger.info('dbus session ended')
  system('ps fja')
  status.success? ? exit : abort
end

# Video recording wrapper
class Recorder
  def self.with(&block)
    return block.yield unless ENV['RECORD_VIDEO_NAME']

    abort 'RECORD_VIDEO requires that a nested kwin wayland be used! (TEST_WITH_KWIN_WAYLAND)' unless ENV['KWIN_PID']

    # Make sure kwin is up. This can be removed once the code was changed to re-exec as part of a kwin
    # subprocess, then the wayland server is ready by the time we get re-executed.
    sleep(5)
    FileUtils.rm_f(ENV['RECORD_VIDEO_NAME'])
    pids = []
    pids << spawn('pipewire')
    pids << spawn('wireplumber')
    pids << spawn(find_program('xdg-desktop-portal-kde'))
    pids << spawn('selenium-webdriver-at-spi-recorder', '--output', ENV.fetch('RECORD_VIDEO_NAME'))
    5.times do
      break if File.exist?(ENV['RECORD_VIDEO_NAME'])

      sleep(1)
    end
    block.yield
  ensure
    if ENV['RECORD_VIDEO_NAME'] && File.size(ENV['RECORD_VIDEO_NAME']) < 256_000
      warn "recording apparently didn't work properly"
    end
    terminate_pids(pids)
  end

  def self.find_program(name)
    @paths ||= ENV.fetch('LD_LIBRARY_PATH', '').split(':').map { |x| "#{x}/libexec" } +
               [
                 '/usr/lib/*/libexec/', # debian
                 '/usr/libexec/', # suse
                 '/usr/lib/' # arch
               ]

    @paths.each do |x|
      path = "#{x}/#{name}"
      return path if Dir.glob(path)&.first
    end
    raise "Could not resolve absolute path for #{name}; searched in #{@paths.join(', ')}"
  end
end

class Driver
  def self.with(datadir, &block)
    pids = []
    pids << spawn({ 'FLASK_ENV' => 'production', 'FLASK_APP' => 'selenium-webdriver-at-spi.py' },
                  'flask', 'run', '--port', PORT, '--no-reload',
                  chdir: datadir)
    block.yield
  ensure
    terminate_pids(pids)
  end
end

PORT = '4723'
$stdout.sync = true # force immediate flushing without internal caching
logger = Logger.new($stdout)

# Tweak the CIs logging rules. They are way too verbose for our purposes
ENV['QT_LOGGING_RULES'] = <<-RULES.gsub(/\s/, '')
  *.debug=true;qt.text.font.db=false;kf.globalaccel.kglobalacceld=false;kf.wayland.client=false;
  qt.quick.hover.*=false;qt.quick.layouts=false;qt.scenegraph.*=false;qt.qml.diskcache=false;qt.text.font.*=false;
  qt.qml.gc.*=false;qt.qpa.wayland.*=false;qt.quick.dirty=false;qt.accessibility.cache=false;qt.v4.asm=false;
  qt.quick.itemview.delegaterecycling=false;qt.opengl.diskcache=false;qt.qpa.fonts=false;kf.kio.workers.http=false;
  qt.quick.pointer.events=false;qt.quick.handler.dispatch=false;qt.quick.mouse.target=false;qt.quick.mouse=false;
  qt.quick.focus=false;qt.text.layout=false;
RULES

dbus_reexec!(logger: logger)
kwin_reexec!
raise 'Failed to set dbus env' unless system('dbus-update-activation-environment', '--all')

logger.info 'Installing dependencies'
datadir = File.absolute_path("#{__dir__}/../share/selenium-webdriver-at-spi/")
if File.exist?("#{datadir}/requirements.txt")
  system('pip3', 'install', '-r', 'requirements.txt', chdir: datadir) || raise
  ENV['PATH'] = "#{Dir.home}/.local/bin:#{ENV.fetch('PATH')}"
end

ret = false
ATSPIBus.new(logger: logger).with do
  Recorder.with do
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
