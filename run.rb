#!/usr/bin/env ruby
# frozen_string_literal: true

# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2022-2023 Harald Sitter <sitter@kde.org>

require 'fileutils'
require 'logger'
require 'shellwords'
require 'tmpdir'

SYSTEMD_ASSISTED_CI = ENV['KDECI_BUILD'] == 'TRUE' && !ENV['KDECI_PLATFORM_PATH'].include?('alpine')

def at_bus_exists?
  return true if SYSTEMD_ASSISTED_CI # when managed by systemd it may be lazily started

  IO.popen(['dbus-send', '--print-reply=literal', '--dest=org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus.ListNames'], 'r') do |io|
    io.read.include?('org.a11y.Bus')
  end
end

def at_bus_address
  IO.popen(['dbus-send', '--print-reply=literal', '--dest=org.a11y.Bus', '/org/a11y/bus', 'org.a11y.Bus.GetAddress'], 'r') do |io|
    io.read.strip
  end
end

def terminate_pgids(pgids)
  (pgids || []).reverse.each do |pgid|
    Process.kill('-TERM', pgid)
    Process.waitpid(pgid)
  rescue Errno::ECHILD => e
    warn "Process group not found #{e}"
  rescue Errno::ESRCH => e
    warn "Process not found #{e}"
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
    ENV['KWIN_XKB_DEFAULT_KEYMAP'] = 'true'
    # Don't set RULES or MODEL, they ought to be valid and we probably don't need to change them from whatever is set!
    ENV['XKB_DEFAULT_LAYOUT'] = 'us'
    ENV['XKB_DEFAULT_VARIANT'] = ''
    ENV['XKB_DEFAULT_OPTIONS'] = ''
    extra_args = []
    extra_args << '--width=' + ENV['COMPOSITOR_WIDTH'] if ENV['COMPOSITOR_WIDTH']
    extra_args << '--height=' + ENV['COMPOSITOR_HEIGHT'] if ENV['COMPOSITOR_HEIGHT']
    extra_args << '--virtual' if ENV['LIBGL_ALWAYS_SOFTWARE']
    extra_args << '--xwayland' if ENV.fetch('TEST_WITH_XWAYLAND', '0').to_i.positive?
    extra_args << '--no-global-shortcuts' if ENV.fetch('TEST_WITHOUT_GLOBAL_SHORTCUTS', '1').to_i.positive?
    # A bit awkward because of how argument parsing works on the kwin side: we must rely on shell word merging for
    # the __FILE__ ARGV bit, separate ARGVs to kwin_wayland would be distinct subprocesses to start but we want
    # one processes with a bunch of arguments.
    exec('kwin_wayland', '--no-lockscreen', *extra_args,
         '--exit-with-session', "#{__FILE__} #{ARGV.shelljoin}", out: "appium_artifact_#{File.basename(ARGV[0])}_kwin_stdout.log")
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
  system('ps fja', out: "appium_artifact_#{File.basename(ARGV[0])}_ps_stdout.log")
  status.success? ? exit : abort
end

# Video recording wrapper
class Recorder
  def self.with(&block)
    pids = []
    if ENV['KWIN_PID'] # Only auto-record if using kwin_wayland
      if ARGV.size >= 1
        # There is at least one argument, it should be the file name of the test to run. Let's just record as that.
        ENV['RECORD_VIDEO_NAME'] = "appium_artifact_#{File.basename(ARGV[0])}.webm"
      elsif ARGV.include?('--selenium-record-video')
        # Extract our own argument and the argument that follows it, then delete them so they don't mess with the
        # actual test.
        ENV['RECORD_VIDEO_NAME'] = ARGV[ARGV.index('--selenium-record-video') + 1]
        ARGV.delete('--selenium-record-video')
        ARGV.delete(ENV['RECORD_VIDEO_NAME'])
      end
    end

    # Yield unless we are recording a video
    return block.yield unless ENV['RECORD_VIDEO_NAME']

    abort 'RECORD_VIDEO requires that a nested kwin wayland be used! (TEST_WITH_KWIN_WAYLAND)' unless ENV['KWIN_PID']

    recording = ENV.fetch('RECORD_VIDEO_NAME')
    start_marker = "#{recording}.started"

    FileUtils.rm_f(recording)
    FileUtils.rm_f(start_marker)

    if ENV.include?('CUSTOM_BUS')
      # Only start auxillary services if we are running a custom bus. Otherwise we'd mess up session services.
      pids << spawn('pipewire')
      pids << spawn('wireplumber')
    end

    20.times do # make sure pipewire is up and kwin is connected already, otherwise recording will definitely fail
      break if system('pw-dump | grep -q kwin_wayland')

      sleep(1)
    end

    pids << spawn('selenium-webdriver-at-spi-recorder', '--output', recording)

    20.times do
      break if File.exist?(start_marker)

      sleep(1)
    end

    unless File.exist?(start_marker)
      warn "Video recording didn't start properly, file was not created #{start_marker}"
      abort "Failed to start video recording. Please talk to sitter!"
    end

    block.yield

  ensure
    # mind that we may skip out of the block above before defining certain variables. Be mindful of what may be undefined.
    terminate_pids(pids)

    if recording # may be undefined if no recording is requested
      unless File.exist?(recording)
        warn "Video recording didn't finish properly, file was not created #{recording}"
        abort "Failed to stop video recording. Please talk to sitter!"
      end

      recording_exists = File.exist?(recording)
      recording_looks_valid = (File.size(recording) < 1024)
      if !recording_exists || !recording_looks_valid
        warn "recording apparently didn't work properly #{recording} exists: #{recording_exists}, size: #{File.size(recording)} #{recording_looks_valid}"
      end
    end
  end
end

class Driver
  def self.with(datadir, &block)
    pids = []
    env = { 'FLASK_ENV' => 'production', 'FLASK_APP' => 'selenium-webdriver-at-spi.py' }
    env['GDK_BACKEND'] = 'wayland' if ENV['KWIN_PID']
    pids << spawn(env,
                  'flask', 'run', '--port', PORT, '--no-reload',
                  chdir: datadir,
                  out: "appium_artifact_#{File.basename(ARGV[0])}_webdriver_stdout.log")
    block.yield
  ensure
    terminate_pids(pids)
  end
end

PORT = ENV.fetch('FLASK_PORT', '4723')
$stdout.sync = true # force immediate flushing without internal caching
ENV['PYTHONUNBUFFERED'] = '0' # same for python subprocesses
logger = Logger.new($stdout)

logger.info 'Installing dependencies'
datadir = File.absolute_path("#{__dir__}/../share/selenium-webdriver-at-spi/")
requirements_installed_marker = "#{Dir.tmpdir}/selenium-requirements-installed"
if !File.exist?(requirements_installed_marker) && File.exist?("#{datadir}/requirements.txt")
  raise 'pip3 not found in PATH!' unless system('which', 'pip3')
  unless system('pip3', 'install', '--disable-pip-version-check', '-r', 'requirements.txt', chdir: datadir,
                out: "appium_artifact_#{File.basename(ARGV[0])}_pip_stdout.log", err: "appium_artifact_#{File.basename(ARGV[0])}_pip_stderr.log")
    unless system('pip3', 'install', '--disable-pip-version-check', '--break-system-packages', '-r', 'requirements.txt',
                  chdir: datadir, out: "appium_artifact_#{File.basename(ARGV[0])}_pip-break_stdout.log")
      raise 'Failed to run pip3 install!'
    end
  end

  if ENV['KDECI_BUILD'] == 'TRUE'
    File.open(requirements_installed_marker, "w") do |file|
      # create an empty file so tests in the same CI container can skip the process
    end
  end
end

if SYSTEMD_ASSISTED_CI
  # Prefer using systemd managed services. It's faster and more reliable than doing this manually.
  # ci-utilities mangles the environment - unmangle it so systemctl actually manages to talk to the daemon instance.
  ENV['DBUS_SESSION_BUS_ADDRESS'] = "unix:path=/run/user/#{`id -u`.strip}/bus"
  ENV['XDG_RUNTIME_DIR'] = "/run/user/#{`id -u`.strip}"
  logger.info 'Starting user services via systemd'
  for service in ['at-spi-dbus-bus.service', 'pipewire.socket', 'wireplumber.service']
    system('systemctl', '--user', 'start', service) || raise("Failed to start #{service} via systemd!")
  end
end

# Just in case the environment is a bit incomplete and doesn't have the dir created. Not having the dir breaks assumptions
# in some software.
FileUtils.mkdir_p(ENV['XDG_RUNTIME_DIR'])

ENV['PATH'] = "#{Dir.home}/.local/bin:#{ENV.fetch('PATH')}"

ret = false

# create a throw-away XDG home, so the test starts with a clean slate
# with every run, and doesn't mess with your local installation
Dir.mktmpdir('selenium') do |xdg_home|
  %w[CACHE CONFIG DATA STATE].each do |d|
    Dir.mkdir("#{xdg_home}/#{d}")
    ENV["XDG_#{d}_HOME"] = "#{xdg_home}/#{d}"
  end

  dbus_reexec!(logger: logger)
  kwin_reexec!
  if ENV['KDECI_BUILD'] == 'TRUE'
    raise 'Failed to set dbus env' unless system('dbus-update-activation-environment', '--all')
  end

  ATSPIBus.new(logger: logger).with do
    # Prevent a race condition in Qt when it tries to figure out the bus address,
    # instead just tell it the address explicitly.
    # https://codereview.qt-project.org/c/qt/qtbase/+/493700/2
    ENV['AT_SPI_BUS_ADDRESS'] = at_bus_address
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
        ret = begin
          system(*ARGV, exception: true)
        rescue RuntimeError # We intentionally let ENOENT raise out of this block
          false
        end
        logger.info 'tests done'
      end
    end
  end
end

logger.info "run.rb exiting #{ret}"
ret ? exit : abort
