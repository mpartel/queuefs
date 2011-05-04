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

require 'fileutils.rb'
include FileUtils

# Set the default umask for all tests
File.umask 0022

EXECUTABLE_PATH = '../src/queuefs'
TESTDIR_NAME = 'tmp_test_queuefs'

# If set to an array of test names, only those will be run
$only_these_tests = nil

# Prepares a test environment with a mounted directory
def test(testcase_title, options = {}, &block)

    queuefs_args = options[:args] || ''

    return unless $only_these_tests == nil or $only_these_tests.member? testcase_title

    puts "--- #{testcase_title} ---"
    puts "[  #{queuefs_args}  ]" unless queuefs_args.empty?

    begin
        Dir.mkdir TESTDIR_NAME
    rescue Exception => ex
        $stderr.puts "ERROR creating testdir at #{TESTDIR_NAME}"
        $stderr.puts ex
        exit! 1
    end

    begin
        Dir.chdir TESTDIR_NAME
        Dir.mkdir 'src'
        Dir.mkdir 'mnt'
    rescue Exception => ex
        $stderr.puts "ERROR preparing testdir at #{TESTDIR_NAME}"
        $stderr.puts ex
        exit! 1
    end

    queuefs_pid = nil
    begin
        cmd = "../#{EXECUTABLE_PATH} #{queuefs_args} src mnt"
        queuefs_pid = Process.fork do
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

    # Wait for queuefs to daemonize itself
    Process.wait queuefs_pid

    # TODO: check that mounting was successful

    testcase_ok = true
    begin
        yield
    rescue Exception => ex
        $stderr.puts "ERROR: testcase `#{testcase_title}' failed"
        $stderr.puts ex
        $stderr.puts ex.backtrace
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


