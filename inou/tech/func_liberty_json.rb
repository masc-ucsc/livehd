#!/bin/ruby

require 'strscan'
require 'byebug'

if ARGV.size < 1
  puts "USAGE: verilog.rb <filenames>"
  exit 1
end

scanner = StringScanner.new("")
srcline = 0
wirename = /\s*(\\?[\-\d\w\\:_\$\.\/\[\]]+)\s*/  #FIXME: do we need to discard the $0??
cellname = /([$_\.\/:\w\-\d\\\[\]]+)\s*/
celltype = /([\.$\w=\-\\']+)\s*/

inmod     = false
incomment = false
intest    = false
inpin     = false
modname   = nil
pin_name  = ""

curly = 0

inputs = []
outputs = []


linen = 0
counter = 0;
print "\{\n"
puts "\"cells\": \[\n"

tmp = ARGV

while file = ARGV.shift
  File.open(file).each_line do |line|

    scanner << line
    linen += 1
    scanner.skip(/\s*/)
    scanner.skip(/\/\*[\n\s\w\W\*]*\*\//)
    scanner.skip(/\/\/[\w\W]*\n/)
    scanner.skip(/`[\w\W]*\n/)
    scanner.skip(/;/)


    curly += 1 if scanner.skip(/\s*library\s*\([\w\W]+\)\s{/)
    scanner.skip(/\s*date\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*revision\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*comment\s*:\s*"[\w\W]+"\s*;/)

    scanner.skip(/\s*technology\s*\([\w\W]+\)\s*;/)
    scanner.skip(/\s*delay_model\s*:\s*[\w\W_]+\s*;/)
    scanner.skip(/\s*in_place_swap_mode\s*:\s*[\w\W_]+\s*;/)
    scanner.skip(/\s*library_features\s*\([\w\W]+\)\s*;/)

    scanner.skip(/\s*time_unit\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*leakage_power_unit\s*:\s*"?[\w\W]+"?\s*;/)
    scanner.skip(/\s*voltage_unit\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*current_unit\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*pulling_resistance_unit\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*capacitive_load_unit\s*\(\d+(.\d+)?,[\s\\]*"?\w+"?\s*\);/)

    scanner.skip(/\s*nom_process\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*nom_temperature\s*:\s*-?\d+\.\d+\s*;/)
    scanner.skip(/\s*nom_voltage\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*driver_model\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*driver_type\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*default_wire_load_capacitance\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*default_wire_load_resistance\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*default_wire_load_area\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_wire_load_mode\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*default_wire_load\s*:\s*"[\w]+"\s*;/)
    scanner.skip(/\s*default_wire_load_selection\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*default_threshold_voltage_group\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*def_sim_opt\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*simulator\s*:\s*"[\w\W]+"\s*;/)

    curly += 1 if scanner.skip(/\s*output_voltage\s*\([\w\W]+\)\s*{/)
    scanner.skip(/\s*vol\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*voh\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*vomin\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*vomax\s*:\s*[\w\W]+\s*;/)

    curly += 1 if scanner.skip(/\s*wire_load\s*\([\w\W]+\)\s*{/)
    scanner.skip(/\s*fanout_length\s*\(\s*"\d+"\s*,[\s\\]*"\d+(\.\d+)?"\s*\)\s*;/)
    scanner.skip(/\s*capacitance\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*resistance\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*area\s*:\s*\d+(\.\d+)?\s*;/)
    scanner.skip(/\s*slope\s*:\s*\d+(\.\d+)?\s*;/)

    curly += 1 if scanner.skip(/\s*wire_load_selection\s*\([\w\W]+\)\s*{/)
    scanner.skip(/\s*wire_load_from_area\s*\((\s*\d+(.\d+)?\s*,?)+[\s\\]*"[\w\W]+"\s*\)\s*;/)

    scanner.skip(/\s*voltage_map\s*\([\w\W]+,\d+\.\d+\)\s*;/)

    scanner.skip(/\s*define\s*\([[\w\W]+,?]+\)\s*;/)

    curly += 1 if scanner.skip(/\s*operating_conditions\s*\([\w\W]+\)\s*{/)
    scanner.skip(/\s*process_corner\s*:\s*"[\w\W]+"\s*;/)
    scanner.skip(/\s*process\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*voltage\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*temperature\s*:\s*-?\d+\.\d+\s*;/)
    scanner.skip(/\s*tree_type\s*:\s*[\w\W_]+\s*;/)

    scanner.skip(/\s*default_operating_conditions\s*:\s*[\w\W_]+\s*;/)
    scanner.skip(/\s*slew_lower_threshold_pct_fall\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*slew_lower_threshold_pct_rise\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*slew_upper_threshold_pct_fall\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*slew_upper_threshold_pct_rise\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*slew_derate_from_library\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*input_threshold_pct_fall\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*input_threshold_pct_rise\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*output_threshold_pct_fall\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*output_threshold_pct_rise\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_leakage_power_density\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_cell_leakage_power\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_inout_pin_cap\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_input_pin_cap\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_output_pin_cap\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_fanout_load\s*:\s*\d+\.\d+\s*;/)
    scanner.skip(/\s*default_max_transition\s*:\s*\d+\.\d+\s*;/)

    curly += 1 if scanner.skip(/\s*normalized_driver_waveform\s*\([\w\W]+\)\s*{/)
    scanner.skip(/\s*driver_waveform_name\s*:\s*[\w\W]+\s*;/)
    scanner.skip(/\s*driver_waveform_rise\s*:\s*[\w\W]+\s*;/)
    scanner.skip(/\s*driver_waveform_fall\s*:\s*[\w\W]+\s*;/)

    curly += 1 if scanner.skip(/\s*power_lut_template\s*\([\w\W]+\)\s*{/)
    curly += 1 if scanner.skip(/\s*lu_table_template\s*\([\w\W]+\)\s*{/)
    scanner.skip(/\s*variable_\d\s*:\s*[\w\W_]+\s*;/)
    scanner.skip(/\s*index_\d\s*\("[\d+\.\d+,?\s*]+"\)\s*;/)
    scanner.skip(/\s*values\s*\("[-?\d+\.\d+,?]+"\)\s*;/)
    #scanner.skip(/\s*values\s*\(((\s*\\\n)*"(-?\d+\.\d+(e[\+-]\d+)?,?\s*)+"(,\s*\\\n)*)+\);/)
    scanner.skip(/\s*values\s*\((\s*(\s*\\\n\s*)*"(-?\d+(\.\d+)?(e[\+-]\d+)?,?\s*)+"(,?\s*\\\n)*)+\s*\);/)

    scanner.skip(/[\s\n]*/)
    scanner.skip(/\/\*[\n\s\w\W\*]*\*\//)
    scanner.skip(/\/\/[\w\W]*\n/)
    scanner.skip(/`[\w\W]*\n/)
    scanner.skip(/;/)
      byebug if linen > 1000

    if(!inmod)
      if(scanner.skip(/cell\s*\("?([\w]+)"?\)\s*{/))
        inmod = true
        curly += 1
        modname = scanner[1]
      end
    end

    if(inmod)
      scanner.skip(/\s*drive_strength\s*:\s*\d+(\.\d+)?\s*;/)
      scanner.skip(/\s*area\s*:\s*\d+(\.\d+)?\s*;/)
      scanner.skip(/\s*slope\s*:\s*\d+(\.\d+)?\s*;/)
      scanner.skip(/\s*cell_footprint\s*:\s*"[\w\W]+"\s*;/)
      scanner.skip(/\s*retention_cell\s*:\s*"[\w\W]+"\s*;/)

      scanner.skip(/\s*dont_touch\s*:\s*\w+\s*;/)
      scanner.skip(/\s*dont_use\s*:\s*\w+\s*;/)
      scanner.skip(/\s*always_on\s*:\s*"?\w+"?\s*;/)
      scanner.skip(/\s*is_decap_cell\s*:\s*"?\w+"?\s*;/)
      scanner.skip(/\s*is_filler_cell\s*:\s*"?\w+"?\s*;/)
      scanner.skip(/\s*is_isolation_cell\s*:\s*"?\w+"?\s*;/)
      scanner.skip(/\s*isolation_cell_enable_pin\s*:\s*"?\w+"?\s*;/)
      scanner.skip(/\s*isolation_cell_data_pin\s*:\s*"?\w+"?\s*;/)
      scanner.skip(/\s*retention_pin\s*\("\w+",\d+\)\s*;/)

      curly += 1 if scanner.skip(/pg_pin\s*\([\w\W]+\)\s*{/)
      scanner.skip(/[\s\n]*voltage_name\s*:\s*[\w\W]+;/)
      scanner.skip(/[\s\n]*pg_type\s*:\s*[\w\W]+;/)

      scanner.skip(/[\s\n]*cell_leakage_power\s*:\s*[\w\W]+;/)
      curly += 1 if scanner.skip(/[\s\n]*leakage_power\s*\(\)\s*{/)
      scanner.skip(/[\s\n]*when\s*:\s*[\w\W]+;/)
      scanner.skip(/[\s\n]*value\s*:\s*"?\d+\.\d+(e[+-]\d+)?"?;/)

      curly += 1 if scanner.skip(/\s*statetable\s*\("?([\w\W]+"?,?)+\)\s*{/)
      scanner.skip(/\s*table\s*:\s*"[\w\W]+"\s*;/)

      scanner.skip(/\s*clock_gating_integrated_cell\s*:[\w\W]+\s*;/)
      scanner.skip(/\s*pin_opposite\s*\("[\w\W]+"\s*,[\s\\]*"[\w\W]+"\s*\)\s*;/)

      if scanner.skip(/\s*test_cell\s*\(\s*\)\s*{/)
        curly += 1
        intest = true
      end
      curly += 1 if scanner.skip(/\s*ff\s*\(("?[\w\W]+"?,?)+\s*\)\s*{/)
      curly += 1 if scanner.skip(/\s*latch\s*\(("?[\w\W]+"?,?)+\s*\)\s*{/)
      scanner.skip(/\s*next_state\s*:\s*"[\w\W]+"\s*;/)
      scanner.skip(/\s*data_in\s*:\s*"[\w\W]+"\s*;/)
      scanner.skip(/\s*enable\s*:\s*"[\w\W]+"\s*;/)
      scanner.skip(/\s*nextstate_type\s*:\s*[\w\W]+\s*;/)
      scanner.skip(/\s*clocked_on\s*:\s*"[\w\W]+"\s*;/)
      scanner.skip(/\s*clear\s*:\s*"[\w\W]+"\s*;/)
      scanner.skip(/\s*preset\s*:\s*"[\w\W]+"\s*;/)
      scanner.skip(/\s*clear_preset_var\d\s*:\s*"?[\w\W]+"?\s*;/)



      if(scanner.skip(/pin\s*\(([\w\W]+)\)\s*{/))
        curly += 1
        pin_name = scanner[1]
        inpin    = true
      end

      if(inpin)

        if(scanner.skip(/\s*direction\s*:\s*"?input"?\s*;/))
           inputs << pin_name if !intest
         elsif(scanner.skip(/\s*direction\s*:\s*"?output"?\s*;/))
           outputs << pin_name if !intest
         elsif(scanner.skip(/\s*direction\s*:\s*"?\w+"?\s*;/))
           #do nothing
        end
        scanner.skip(/\s*signal_type\s*:\s*[\w\W]+\s*;/)

        scanner.skip(/\s*related_power_pin\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*related_ground_pin\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*related_output_pin\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*capacitance\s*:\s*\d+(\.\d+)?\s*;/)
        scanner.skip(/\s*fall_capacitance\s*:\s*\d+\.\d+\s*;/)
        scanner.skip(/\s*rise_capacitance\s*:\s*\d+\.\d+\s*;/)
        scanner.skip(/\s*min_pulse_width_low\s*:\s*\d+\.\d+\s*;/)
        scanner.skip(/\s*min_pulse_width_high\s*:\s*\d+\.\d+\s*;/)
        scanner.skip(/\s*rise_capacitance_range\s*\(\s*(\d+\.\d+,?)+\s*\)\s*;/)
        scanner.skip(/\s*fall_capacitance_range\s*\(\s*(\d+\.\d+,?)+\s*\)\s*;/)

        scanner.skip(/\s*max_capacitance\s*:\s*\d+\.\d+\s*;/)
        scanner.skip(/\s*min_capacitance\s*:\s*\d+\.\d+\s*;/)
        scanner.skip(/\s*fanout_load\s*:\s*\d+\.\d+\s*;/)
        scanner.skip(/\s*function\s*:\s*"?[\w\W]+"?\s*;/)
        scanner.skip(/\s*power_down_function\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*output_voltage\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*state_function\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*three_state\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*max_transition\s*:\s*\d+\.\d+\s*;/)

        scanner.skip(/\s*internal_node\s*:\s*"?\w+"?\s*;/)
        scanner.skip(/\s*clock_gate_clock_pin\s*:\s*\w+\s*;/)
        scanner.skip(/\s*clock_gate_enable_pin\s*:\s*\w+\s*;/)
        scanner.skip(/\s*clock_gate_test_pin\s*:\s*\w+\s*;/)
        scanner.skip(/\s*clock_gate_out_pin\s*:\s*\w+\s*;/)
        scanner.skip(/\s*clock\s*:\s*\w+\s*;/)


        scanner.skip(/\s*fall_capacitance_range\s*\(\s*(\d+\.\d+,?\s*)+\)\s*;/)
        scanner.skip(/\s*rise_capacitance_range\s*\(\s*(\d+\.\d+,?\s*)+\)\s*;/)

        curly += 1 if scanner.skip(/\s*internal_power\s*\(\s*\)\s*{/)
        curly += 1 if scanner.skip(/\s*fall_power\s*\(\s*[\w\W]+\)\s*{/)
        curly += 1 if scanner.skip(/\s*rise_power\s*\(\s*[\w\W]+\)\s*{/)


        curly += 1 if scanner.skip(/\s*timing\s*\(\s*\)\s*{/)
        scanner.skip(/\s*related_pin\s*:\s*"[\w\W]+"\s*;/)
        scanner.skip(/\s*timing_sense\s*:\s*[\w\W_]+\s*;/)
        scanner.skip(/\s*timing_type\s*:\s*"?\w+"?\s*;/)
        scanner.skip(/\s*sdf_cond\s*:\s*"[\w\W_]+"\s*;/)
        scanner.skip(/\s*clk_width\s*:\s*"[\w\d+-_]+"\s*;/)
        curly += 1 if scanner.skip(/\s*cell_fall\s*\([\w\W_]+\)\s*{/)
        curly += 1 if scanner.skip(/\s*cell_rise\s*\([\w\W_]+\)\s*{/)
        curly += 1 if scanner.skip(/\s*fall_transition\s*\([\w\W_]+\)\s*{/)
        curly += 1 if scanner.skip(/\s*rise_transition\s*\([\w\W_]+\)\s*{/)
        curly += 1 if scanner.skip(/\s*fall_constraint\s*\([\w\W_]+\)\s*{/)
        curly += 1 if scanner.skip(/\s*rise_constraint\s*\([\w\W_]+\)\s*{/)
      end

    end
    if scanner.skip(/}/)
      curly -= 1
      if curly == 3 and intest and inpin
        inpin = false
      elsif curly == 2 and intest
        intest = false
      elsif curly == 2 and inpin
        inpin = false
      elsif(curly == 1)
        if inmod
          if(counter > 0)
            print(",\n")
          end
          puts "\{ "

          if inputs.size >= 1 or outputs.size >= 1
            puts "\"cell\": \"#{modname}\","
          else
            puts "\"cell\": \"#{modname}\""
          end

          if inputs.size >= 1
            print "\"inps\": \["
            print inputs.map  { |a| "\"#{a}\""}.join(",\t")
            if outputs.size >= 1
              puts "\],"
            else
              puts "\]"
            end
          end

          if outputs.size >= 1
            print "\"outs\": \["
            print outputs.map  { |a| "\"#{a}\""}.join(",\t")
            puts "\]"
          end

          print "\}"
          inmod = false
          modname = nil
          inputs.clear
          outputs.clear
          counter += 1
        end
      end
    end

  end
end
print "\n\]\n"
print "\}"
