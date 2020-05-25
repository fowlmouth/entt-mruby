#!/usr/bin/env ruby

require'optparse'

opts = {
  entt: nil,
  cc: 'clang++',
  output: 'mruby-test',
  cfiles: 'mruby-test.cc',
}

OptionParser.new do |o|
  o.on '--entt=VALUE', 'path to entt directory' do |entt|
    opts[:entt] = entt
  end

  o.on '--cc=COMPILER', 'c++ compiler' do |cc|
    opts[:cc] = cc
  end

  o.on '--output=BINARYNAME', 'name of binary output' do |file|
    opts[:output] = file
  end

  o.on '--cfiles=FILES', 'c++ source files (space separated)' do |cfiles|
    opts[:cfiles] = cfiles
  end

  o.on '-I=DIR', 'c++ header file dir' do |dir|
    (opts[:I] ||= []) << dir
  end

end.parse!

fail = false
opts.each {|k,v|
  if v.nil?
    fail = true
    puts "Missing option: #{k}"
  end
}
abort if fail

cmd = "#{opts[:cc]} \
  -g -std=c++2a \
  -I #{opts[:entt]} \
  #{opts[:I].map{|dir| "-I#{dir}"}.join(' ') if opts[:I]} \
  -I ../include \
  #{opts[:cfiles]} \
  -o #{opts[:output]} \
  -lmruby"

puts "Running command: #{cmd}"
exit Kernel.system cmd
