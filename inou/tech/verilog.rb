#!/bin/ruby

require 'strscan'

if ARGV.size < 1
  puts "USAGE: verilog.rb <filename>"
  exit 1
end

scanner = StringScanner.new("")
srcline = 0
wirename = /\s*(\\?[\-\d\w\\:_\$\.\/\[\]]+)\s*/  #FIXME: do we need to discard the $0??
cellname = /([$_\.\/:\w\-\d\\\[\]]+)\s*/
celltype = /([\.$\w=\-\\']+)\s*/

inmod     = false
incomment = false
modname = nil

inputs = []
outputs = []


linen = 0

File.open(ARGV[0]).each_line do |line|
  scanner << line
  linen += 1
  scanner.skip(/\s*/)
  scanner.skip(/\/\*[\n\s\w\W\*]*\*\//)
  scanner.skip(/\/\/[\w\W]*\n/)
  scanner.skip(/`[\w\W]*\n/)

  if(!inmod)
    if(scanner.skip(/module\s*/))
      #puts "found module"
      inmod = true
    end
  end

  if(inmod)
    scanner.skip(/[\s\n]*/)
    if(modname == nil)
      if(scanner.skip(/\\?(\$?\w+)/))
        modname = scanner[1]
      else
        next
      end
    end

    if(scanner.skip(/\s*\(/))
       while(scanner.skip(/\s*(\w+)\s*,\s*/))
       end
       scanner.skip(/\w+\s*\)\s*;[\s\n]*/)
    end

    if(scanner.skip(/\s*input\s+/))
      scanner.skip(/\[\d+:\d+\]/)
      while(scanner.skip(/\s*(\w+)\s*[,;]?\s*/))
       inputs << scanner[1]
       break if(scanner.rest.match(/\s*output\s*/))
      end
      scanner.skip(/\s*;\s*/)
    end

    if(scanner.skip(/\s*output\s+/))
      scanner.skip(/reg\s+/)
      scanner.skip(/\[\d+:\d+\]/)
      while(scanner.skip(/\s*(\w+)\s*[,;]?\s*/))
       outputs << scanner[1]
       break if(scanner.rest.match(/\s*input\s*/))
      end
      scanner.skip(/\s*;\s*/)
    end

    #ignore everything else
    if(scanner.skip_until(/endmodule/))
      puts "- cell: #{modname}"
      puts "  inps:"
      puts inputs.map  { |a| "  - #{a}"}.join("\n")
      puts "  outs:"
      puts outputs.map { |a| "  - #{a}"}.join("\n")
      inmod = false
      modname = nil
      inputs.clear
      outputs.clear
    end
  end
end


