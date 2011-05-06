#!/usr/bin/env ruby                                                          
# Copyright (c) 2011 Martin Pärtel <martin.partel@gmail.com>
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
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


