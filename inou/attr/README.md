## Commands:

`
lg "inou.liveparse files:SingleCycleCPU.v |> inou.verilog |> pass.lnast_tolg |> pass.cprop |> pass.bitwidth |> pass.cprop |> inou.attr.load files:color.json |> inou.attr.save files:color_save.json"
`

color_save.json will get overwritten everytime you run the command.

<br>

## For example pattern of color.json:
`
[{
		"class": "livehd.lgraph.color",
		"modules": [{
			"name": "ALUControl",
			"node_colors": {
				"_72_": 10,
				"_73_": 50,
        "_74_": 55
			}
		}, {
			"name": "SingleCycleCPU",
			"node_colors": {
				"_570_": 15,
				"_571_": 51,
        "_572_": 52,
        "_569_": 16
			}
		}, {
    "name": "MaxPeriodFibonacciLFSR_NL",
    "node_colors": {
      "_13_": 12,
      "_14_": 13,
      "_15_": 14
    }
    }]

	},
	{
		"class": "livehd.lgraph.power"
	}
]
`
