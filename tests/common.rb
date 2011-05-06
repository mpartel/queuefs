#!/usr/bin/env ruby
#
#   Copyright 2006,2007,2008,2009,2010 Martin Pärtel <martin.partel@gmail.com>
#
#   This file is part of queuefs.
#
#   queuefs is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 2 of the License, or
#   (at your option) any later version.
#
#   queuefs is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with queuefs.  If not, see <http://www.gnu.org/licenses/>.
#


require 'rubygems'
require 'escape'
require 'fileutils'
include FileUtils

# Set the default umask for all tests
File.umask 0022

EXECUTABLE_PATH = '../src/queuefs'
TESTDIR_NAME = 'tmp_test_queuefs'

$queuefs_pid = nil
$queuefs_daemon_pid = nil

# Prepares a test environment with a mounted directory
def test(testcase_title, options = {}, &block)

    queuefs_opts = options[:options] || ''
    cmd_template = options[:cmd] || 'echo {} >> ../logfile'
    debug_output = options[:debug] || false
    
    if debug_output
        cmd_template += " && echo \"JOB DONE: {}\""
    end

    puts "--- #{testcase_title} ---"
    puts "[  #{queuefs_opts}  ]" unless queuefs_opts.empty?

    begin
        Dir.mkdir TESTDIR_NAME
    rescue Exception => ex
        $stderr.puts "ERROR creating testdir at #{TESTDIR_NAME}"
        $stderr.puts ex
        exit! 1
    end

    begin
        Dir.chdir TESTDIR_NAME
        touch 'logfile'
        Dir.mkdir 'src'
        Dir.mkdir 'mnt'
    rescue Exception => ex
        $stderr.puts "ERROR preparing testdir at #{TESTDIR_NAME}"
        $stderr.puts ex
        exit! 1
    end

    $queuefs_pid = nil
    $queuefs_daemon_pid = nil
    begin
        cmd = Escape.shell_command([
            "../#{EXECUTABLE_PATH}",
            queuefs_opts.split(/\s+/),
            "-d",
            "src",
            "mnt",
            cmd_template
        ].flatten.reject(&:empty?)).to_s
        $queuefs_pid = Process.fork do
            unless debug_output
                $stdout.close
                $stderr.close
            end
            exec cmd
            exit! 127
        end
    rescue Exception => ex
        $stderr.puts "ERROR running queuefs"
        $stderr.puts ex
        Dir.chdir '..'
        system("rm -Rf #{TESTDIR_NAME}")
        exit! 1
    end

    # Wait for the mounting to complete
    while process_exists($queuefs_pid) && !`mount`.include?(TESTDIR_NAME)
        sleep 0.01
    end
    
    testcase_ok = true
    
    $queuefs_daemon_pid = child_pids_of($queuefs_pid)[0]
    if $queuefs_daemon_pid.nil?
        $stderr.puts "ERROR: failed to find daemon PID"
        testcase_ok = false
    end
    
    begin
        yield if testcase_ok
    rescue Exception => ex
        $stderr.puts "ERROR: testcase `#{testcase_title}' failed"
        $stderr.puts
        $stderr.puts ex
        $stderr.puts ex.backtrace
        $stderr.puts
        $stderr.puts "Logfile:"
        $stderr.puts IO.read("logfile")
        testcase_ok = false
    end

    begin
        unless system(umount_cmd + ' mnt')
            raise Exception.new(umount_cmd + " failed with status #{$?}")
        end
    rescue Exception => ex
        $stderr.puts "ERROR: failed to umount"
        $stderr.puts ex
        $stderr.puts ex.backtrace
        testcase_ok = false
    end
    
    Process.wait $queuefs_pid
    
    $queuefs_pid = nil

    begin
        Dir.chdir '..'
    rescue Exception => ex
        $stderr.puts "ERROR: failed to exit test env"
        $stderr.puts ex
        $stderr.puts ex.backtrace
        exit! 1
    end

    unless system "rm -Rf #{TESTDIR_NAME}"
        $stderr.puts "ERROR: failed to clear test directory"
        exit! 1
    end

    if testcase_ok
        puts "OK"
    else
        exit! 1
    end
end

def umount_cmd
    if `which fusermount`.strip.empty?
    then 'umount'
    else 'fusermount -uz'
    end
end

def process_exists(pid)
    system("kill -s 0 #{pid}")
end

def child_pids_of(pid)
    table = `ps -o pid= -o ppid=`
    result = []
    table.each_line do |line|
        child, parent = line.strip.split(/\s+/).map &:to_i
        if parent == pid
            result << child
        end
    end
    result.sort!
    result
end

def assert
    raise Exception.new('test failed') unless yield
end

def assert_exception(ex)
    begin
        yield
    rescue ex
        return
    end
    raise Exception.new('expected exception ' + ex.to_s)
end


