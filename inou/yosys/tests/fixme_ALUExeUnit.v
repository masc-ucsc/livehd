

module local_MulDiv( // @[:boom.system.TestHarness.BoomConfig.fir@141163.2]
  input         clock, // @[:boom.system.TestHarness.BoomConfig.fir@141164.4]
  input         reset, // @[:boom.system.TestHarness.BoomConfig.fir@141165.4]
  output        io_req_ready, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  input         io_req_valid, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  input  [3:0]  io_req_bits_fn, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  input         io_req_bits_dw, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  input  [63:0] io_req_bits_in1, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  input  [63:0] io_req_bits_in2, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  input         io_kill, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  input         io_resp_ready, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  output        io_resp_valid, // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
  output [63:0] io_resp_bits_data // @[:boom.system.TestHarness.BoomConfig.fir@141166.4]
);
  reg [2:0] state; // @[Multiplier.scala 51:18:boom.system.TestHarness.BoomConfig.fir@141171.4]
  reg [31:0] _RAND_0;
  reg  req_dw; // @[Multiplier.scala 53:16:boom.system.TestHarness.BoomConfig.fir@141172.4]
  reg [31:0] _RAND_1;
  reg [6:0] count; // @[Multiplier.scala 54:18:boom.system.TestHarness.BoomConfig.fir@141173.4]
  reg [31:0] _RAND_2;
  reg  neg_out; // @[Multiplier.scala 57:20:boom.system.TestHarness.BoomConfig.fir@141174.4]
  reg [31:0] _RAND_3;
  reg  isHi; // @[Multiplier.scala 58:17:boom.system.TestHarness.BoomConfig.fir@141175.4]
  reg [31:0] _RAND_4;
  reg  resHi; // @[Multiplier.scala 59:18:boom.system.TestHarness.BoomConfig.fir@141176.4]
  reg [31:0] _RAND_5;
  reg [64:0] divisor; // @[Multiplier.scala 60:20:boom.system.TestHarness.BoomConfig.fir@141177.4]
  reg [95:0] _RAND_6;
  reg [129:0] remainder; // @[Multiplier.scala 61:22:boom.system.TestHarness.BoomConfig.fir@141178.4]
  reg [159:0] _RAND_7;
  wire [3:0] _T_22; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141179.4]
  wire  cmdMul; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141180.4]
  wire [3:0] _T_25; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141182.4]
  wire  _T_26; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141183.4]
  wire [3:0] _T_27; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141184.4]
  wire  _T_28; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141185.4]
  wire  cmdHi; // @[Decode.scala 15:30:boom.system.TestHarness.BoomConfig.fir@141187.4]
  wire [3:0] _T_31; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141188.4]
  wire  _T_32; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141189.4]
  wire [3:0] _T_33; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141190.4]
  wire  _T_34; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141191.4]
  wire  lhsSigned; // @[Decode.scala 15:30:boom.system.TestHarness.BoomConfig.fir@141193.4]
  wire  _T_38; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141195.4]
  wire  rhsSigned; // @[Decode.scala 15:30:boom.system.TestHarness.BoomConfig.fir@141197.4]
  wire  _T_41; // @[Multiplier.scala 78:62:boom.system.TestHarness.BoomConfig.fir@141202.4]
  wire  _T_43; // @[Multiplier.scala 81:38:boom.system.TestHarness.BoomConfig.fir@141204.4]
  wire  _T_44; // @[Multiplier.scala 81:48:boom.system.TestHarness.BoomConfig.fir@141205.4]
  wire  _T_45; // @[Multiplier.scala 81:29:boom.system.TestHarness.BoomConfig.fir@141206.4]
  wire  lhs_sign; // @[Multiplier.scala 81:23:boom.system.TestHarness.BoomConfig.fir@141207.4]
  wire [31:0] _T_47; // @[Bitwise.scala 72:12:boom.system.TestHarness.BoomConfig.fir@141209.4]
  wire [31:0] _T_48; // @[Multiplier.scala 82:43:boom.system.TestHarness.BoomConfig.fir@141210.4]
  wire [31:0] _T_49; // @[Multiplier.scala 82:17:boom.system.TestHarness.BoomConfig.fir@141211.4]
  wire [31:0] _T_50; // @[Multiplier.scala 83:15:boom.system.TestHarness.BoomConfig.fir@141212.4]
  wire [63:0] lhs_in; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141213.4]
  wire  _T_53; // @[Multiplier.scala 81:38:boom.system.TestHarness.BoomConfig.fir@141216.4]
  wire  _T_54; // @[Multiplier.scala 81:48:boom.system.TestHarness.BoomConfig.fir@141217.4]
  wire  _T_55; // @[Multiplier.scala 81:29:boom.system.TestHarness.BoomConfig.fir@141218.4]
  wire  rhs_sign; // @[Multiplier.scala 81:23:boom.system.TestHarness.BoomConfig.fir@141219.4]
  wire [31:0] _T_57; // @[Bitwise.scala 72:12:boom.system.TestHarness.BoomConfig.fir@141221.4]
  wire [31:0] _T_58; // @[Multiplier.scala 82:43:boom.system.TestHarness.BoomConfig.fir@141222.4]
  wire [31:0] _T_59; // @[Multiplier.scala 82:17:boom.system.TestHarness.BoomConfig.fir@141223.4]
  wire [31:0] _T_60; // @[Multiplier.scala 83:15:boom.system.TestHarness.BoomConfig.fir@141224.4]
  wire [63:0] rhs_in; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141225.4]
  wire [64:0] _T_61; // @[Multiplier.scala 88:29:boom.system.TestHarness.BoomConfig.fir@141226.4]
  wire [65:0] _T_62; // @[Multiplier.scala 88:37:boom.system.TestHarness.BoomConfig.fir@141227.4]
  wire [65:0] _T_63; // @[Multiplier.scala 88:37:boom.system.TestHarness.BoomConfig.fir@141228.4]
  wire [64:0] subtractor; // @[Multiplier.scala 88:37:boom.system.TestHarness.BoomConfig.fir@141229.4]
  wire [63:0] _T_64; // @[Multiplier.scala 89:36:boom.system.TestHarness.BoomConfig.fir@141230.4]
  wire [63:0] _T_65; // @[Multiplier.scala 89:57:boom.system.TestHarness.BoomConfig.fir@141231.4]
  wire [63:0] result; // @[Multiplier.scala 89:19:boom.system.TestHarness.BoomConfig.fir@141232.4]
  wire [64:0] _T_66; // @[Multiplier.scala 90:27:boom.system.TestHarness.BoomConfig.fir@141233.4]
  wire [64:0] _T_67; // @[Multiplier.scala 90:27:boom.system.TestHarness.BoomConfig.fir@141234.4]
  wire [63:0] negated_remainder; // @[Multiplier.scala 90:27:boom.system.TestHarness.BoomConfig.fir@141235.4]
  wire  _T_68; // @[Multiplier.scala 92:39:boom.system.TestHarness.BoomConfig.fir@141236.4]
  wire  _T_69; // @[Multiplier.scala 93:20:boom.system.TestHarness.BoomConfig.fir@141238.6]
  wire  _T_70; // @[Multiplier.scala 96:18:boom.system.TestHarness.BoomConfig.fir@141242.6]
  wire  _T_71; // @[Multiplier.scala 101:39:boom.system.TestHarness.BoomConfig.fir@141248.4]
  wire  _T_72; // @[Multiplier.scala 106:39:boom.system.TestHarness.BoomConfig.fir@141254.4]
  wire [64:0] _T_73; // @[Multiplier.scala 107:31:boom.system.TestHarness.BoomConfig.fir@141256.6]
  wire [128:0] _T_75; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141258.6]
  wire  _T_76; // @[Multiplier.scala 108:31:boom.system.TestHarness.BoomConfig.fir@141259.6]
  wire [63:0] _T_77; // @[Multiplier.scala 109:24:boom.system.TestHarness.BoomConfig.fir@141260.6]
  wire [64:0] _T_78; // @[Multiplier.scala 110:23:boom.system.TestHarness.BoomConfig.fir@141261.6]
  wire [64:0] _T_79; // @[Multiplier.scala 110:37:boom.system.TestHarness.BoomConfig.fir@141262.6]
  wire [64:0] _T_80; // @[Multiplier.scala 111:26:boom.system.TestHarness.BoomConfig.fir@141263.6]
  wire [7:0] _T_81; // @[Multiplier.scala 112:38:boom.system.TestHarness.BoomConfig.fir@141264.6]
  wire [8:0] _T_82; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141265.6]
  wire [8:0] _T_83; // @[Multiplier.scala 112:60:boom.system.TestHarness.BoomConfig.fir@141266.6]
  wire [64:0] _GEN_37; // @[Multiplier.scala 112:67:boom.system.TestHarness.BoomConfig.fir@141267.6]
  wire [73:0] _T_84; // @[Multiplier.scala 112:67:boom.system.TestHarness.BoomConfig.fir@141267.6]
  wire [73:0] _GEN_38; // @[Multiplier.scala 112:76:boom.system.TestHarness.BoomConfig.fir@141268.6]
  wire [73:0] _T_86; // @[Multiplier.scala 112:76:boom.system.TestHarness.BoomConfig.fir@141269.6]
  wire [73:0] _T_87; // @[Multiplier.scala 112:76:boom.system.TestHarness.BoomConfig.fir@141270.6]
  wire [55:0] _T_88; // @[Multiplier.scala 113:38:boom.system.TestHarness.BoomConfig.fir@141271.6]
  wire [73:0] _T_89; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141272.6]
  wire [129:0] _T_90; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141273.6]
  wire  _T_91; // @[Multiplier.scala 114:32:boom.system.TestHarness.BoomConfig.fir@141274.6]
  wire  _T_92; // @[Multiplier.scala 114:57:boom.system.TestHarness.BoomConfig.fir@141275.6]
  wire [10:0] _T_93; // @[Multiplier.scala 116:56:boom.system.TestHarness.BoomConfig.fir@141276.6]
  wire [5:0] _T_94; // @[Multiplier.scala 116:72:boom.system.TestHarness.BoomConfig.fir@141277.6]
  wire [64:0] _T_95; // @[Multiplier.scala 116:46:boom.system.TestHarness.BoomConfig.fir@141278.6]
  wire [63:0] _T_96; // @[Multiplier.scala 116:91:boom.system.TestHarness.BoomConfig.fir@141279.6]
  wire  _T_97; // @[Multiplier.scala 117:47:boom.system.TestHarness.BoomConfig.fir@141280.6]
  wire  _T_99; // @[Multiplier.scala 117:81:boom.system.TestHarness.BoomConfig.fir@141282.6]
  wire  _T_100; // @[Multiplier.scala 117:72:boom.system.TestHarness.BoomConfig.fir@141283.6]
  wire  _T_101; // @[Multiplier.scala 118:7:boom.system.TestHarness.BoomConfig.fir@141284.6]
  wire  _T_102; // @[Multiplier.scala 117:87:boom.system.TestHarness.BoomConfig.fir@141285.6]
  wire [63:0] _T_103; // @[Multiplier.scala 118:26:boom.system.TestHarness.BoomConfig.fir@141286.6]
  wire [63:0] _T_104; // @[Multiplier.scala 118:24:boom.system.TestHarness.BoomConfig.fir@141287.6]
  wire  _T_105; // @[Multiplier.scala 118:37:boom.system.TestHarness.BoomConfig.fir@141288.6]
  wire  _T_106; // @[Multiplier.scala 118:13:boom.system.TestHarness.BoomConfig.fir@141289.6]
  wire [11:0] _T_108; // @[Multiplier.scala 119:36:boom.system.TestHarness.BoomConfig.fir@141291.6]
  wire [11:0] _T_109; // @[Multiplier.scala 119:36:boom.system.TestHarness.BoomConfig.fir@141292.6]
  wire [10:0] _T_110; // @[Multiplier.scala 119:36:boom.system.TestHarness.BoomConfig.fir@141293.6]
  wire [5:0] _T_111; // @[Multiplier.scala 119:60:boom.system.TestHarness.BoomConfig.fir@141294.6]
  wire [128:0] _T_112; // @[Multiplier.scala 119:27:boom.system.TestHarness.BoomConfig.fir@141295.6]
  wire [64:0] _T_113; // @[Multiplier.scala 120:37:boom.system.TestHarness.BoomConfig.fir@141296.6]
  wire [129:0] _T_114; // @[Multiplier.scala 120:55:boom.system.TestHarness.BoomConfig.fir@141297.6]
  wire [63:0] _T_115; // @[Multiplier.scala 120:82:boom.system.TestHarness.BoomConfig.fir@141298.6]
  wire [128:0] _T_116; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141299.6]
  wire [64:0] _T_117; // @[Multiplier.scala 121:34:boom.system.TestHarness.BoomConfig.fir@141300.6]
  wire [63:0] _T_118; // @[Multiplier.scala 121:67:boom.system.TestHarness.BoomConfig.fir@141301.6]
  wire [65:0] _T_119; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141302.6]
  wire [129:0] _T_120; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141303.6]
  wire [6:0] _T_122; // @[Multiplier.scala 123:20:boom.system.TestHarness.BoomConfig.fir@141306.6]
  wire  _T_123; // @[Multiplier.scala 124:25:boom.system.TestHarness.BoomConfig.fir@141308.6]
  wire  _T_124; // @[Multiplier.scala 124:16:boom.system.TestHarness.BoomConfig.fir@141309.6]
  wire  _T_125; // @[Multiplier.scala 129:39:boom.system.TestHarness.BoomConfig.fir@141315.4]
  wire  _T_126; // @[Multiplier.scala 133:28:boom.system.TestHarness.BoomConfig.fir@141317.6]
  wire [63:0] _T_127; // @[Multiplier.scala 134:24:boom.system.TestHarness.BoomConfig.fir@141318.6]
  wire [63:0] _T_128; // @[Multiplier.scala 134:45:boom.system.TestHarness.BoomConfig.fir@141319.6]
  wire [63:0] _T_129; // @[Multiplier.scala 134:14:boom.system.TestHarness.BoomConfig.fir@141320.6]
  wire  _T_131; // @[Multiplier.scala 134:67:boom.system.TestHarness.BoomConfig.fir@141322.6]
  wire [127:0] _T_132; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141323.6]
  wire [128:0] _T_133; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141324.6]
  wire  _T_134; // @[Multiplier.scala 138:17:boom.system.TestHarness.BoomConfig.fir@141326.6]
  wire  _T_138; // @[Multiplier.scala 146:24:boom.system.TestHarness.BoomConfig.fir@141335.6]
  wire  _T_141; // @[Multiplier.scala 146:30:boom.system.TestHarness.BoomConfig.fir@141338.6]
  wire [63:0] _T_143; // @[Multiplier.scala 150:36:boom.system.TestHarness.BoomConfig.fir@141340.6]
  wire [31:0] _T_144; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141341.6]
  wire [31:0] _T_145; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141342.6]
  wire  _T_146; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141343.6]
  wire [15:0] _T_147; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141344.6]
  wire [15:0] _T_148; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141345.6]
  wire  _T_149; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141346.6]
  wire [7:0] _T_150; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141347.6]
  wire [7:0] _T_151; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141348.6]
  wire  _T_152; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141349.6]
  wire [3:0] _T_153; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141350.6]
  wire [3:0] _T_154; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141351.6]
  wire  _T_155; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141352.6]
  wire  _T_156; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141353.6]
  wire  _T_157; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141354.6]
  wire  _T_158; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141355.6]
  wire [1:0] _T_159; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141356.6]
  wire [1:0] _T_160; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141357.6]
  wire  _T_161; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141358.6]
  wire  _T_162; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141359.6]
  wire  _T_163; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141360.6]
  wire [1:0] _T_164; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141361.6]
  wire [1:0] _T_165; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141362.6]
  wire [1:0] _T_166; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141363.6]
  wire [2:0] _T_167; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141364.6]
  wire [3:0] _T_168; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141365.6]
  wire [3:0] _T_169; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141366.6]
  wire  _T_170; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141367.6]
  wire  _T_171; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141368.6]
  wire  _T_172; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141369.6]
  wire  _T_173; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141370.6]
  wire [1:0] _T_174; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141371.6]
  wire [1:0] _T_175; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141372.6]
  wire  _T_176; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141373.6]
  wire  _T_177; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141374.6]
  wire  _T_178; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141375.6]
  wire [1:0] _T_179; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141376.6]
  wire [1:0] _T_180; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141377.6]
  wire [1:0] _T_181; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141378.6]
  wire [2:0] _T_182; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141379.6]
  wire [2:0] _T_183; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141380.6]
  wire [3:0] _T_184; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141381.6]
  wire [7:0] _T_185; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141382.6]
  wire [7:0] _T_186; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141383.6]
  wire  _T_187; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141384.6]
  wire [3:0] _T_188; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141385.6]
  wire [3:0] _T_189; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141386.6]
  wire  _T_190; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141387.6]
  wire  _T_191; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141388.6]
  wire  _T_192; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141389.6]
  wire  _T_193; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141390.6]
  wire [1:0] _T_194; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141391.6]
  wire [1:0] _T_195; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141392.6]
  wire  _T_196; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141393.6]
  wire  _T_197; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141394.6]
  wire  _T_198; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141395.6]
  wire [1:0] _T_199; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141396.6]
  wire [1:0] _T_200; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141397.6]
  wire [1:0] _T_201; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141398.6]
  wire [2:0] _T_202; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141399.6]
  wire [3:0] _T_203; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141400.6]
  wire [3:0] _T_204; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141401.6]
  wire  _T_205; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141402.6]
  wire  _T_206; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141403.6]
  wire  _T_207; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141404.6]
  wire  _T_208; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141405.6]
  wire [1:0] _T_209; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141406.6]
  wire [1:0] _T_210; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141407.6]
  wire  _T_211; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141408.6]
  wire  _T_212; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141409.6]
  wire  _T_213; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141410.6]
  wire [1:0] _T_214; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141411.6]
  wire [1:0] _T_215; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141412.6]
  wire [1:0] _T_216; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141413.6]
  wire [2:0] _T_217; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141414.6]
  wire [2:0] _T_218; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141415.6]
  wire [3:0] _T_219; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141416.6]
  wire [3:0] _T_220; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141417.6]
  wire [4:0] _T_221; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141418.6]
  wire [15:0] _T_222; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141419.6]
  wire [15:0] _T_223; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141420.6]
  wire  _T_224; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141421.6]
  wire [7:0] _T_225; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141422.6]
  wire [7:0] _T_226; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141423.6]
  wire  _T_227; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141424.6]
  wire [3:0] _T_228; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141425.6]
  wire [3:0] _T_229; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141426.6]
  wire  _T_230; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141427.6]
  wire  _T_231; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141428.6]
  wire  _T_232; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141429.6]
  wire  _T_233; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141430.6]
  wire [1:0] _T_234; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141431.6]
  wire [1:0] _T_235; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141432.6]
  wire  _T_236; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141433.6]
  wire  _T_237; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141434.6]
  wire  _T_238; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141435.6]
  wire [1:0] _T_239; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141436.6]
  wire [1:0] _T_240; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141437.6]
  wire [1:0] _T_241; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141438.6]
  wire [2:0] _T_242; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141439.6]
  wire [3:0] _T_243; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141440.6]
  wire [3:0] _T_244; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141441.6]
  wire  _T_245; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141442.6]
  wire  _T_246; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141443.6]
  wire  _T_247; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141444.6]
  wire  _T_248; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141445.6]
  wire [1:0] _T_249; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141446.6]
  wire [1:0] _T_250; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141447.6]
  wire  _T_251; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141448.6]
  wire  _T_252; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141449.6]
  wire  _T_253; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141450.6]
  wire [1:0] _T_254; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141451.6]
  wire [1:0] _T_255; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141452.6]
  wire [1:0] _T_256; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141453.6]
  wire [2:0] _T_257; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141454.6]
  wire [2:0] _T_258; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141455.6]
  wire [3:0] _T_259; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141456.6]
  wire [7:0] _T_260; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141457.6]
  wire [7:0] _T_261; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141458.6]
  wire  _T_262; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141459.6]
  wire [3:0] _T_263; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141460.6]
  wire [3:0] _T_264; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141461.6]
  wire  _T_265; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141462.6]
  wire  _T_266; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141463.6]
  wire  _T_267; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141464.6]
  wire  _T_268; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141465.6]
  wire [1:0] _T_269; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141466.6]
  wire [1:0] _T_270; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141467.6]
  wire  _T_271; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141468.6]
  wire  _T_272; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141469.6]
  wire  _T_273; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141470.6]
  wire [1:0] _T_274; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141471.6]
  wire [1:0] _T_275; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141472.6]
  wire [1:0] _T_276; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141473.6]
  wire [2:0] _T_277; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141474.6]
  wire [3:0] _T_278; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141475.6]
  wire [3:0] _T_279; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141476.6]
  wire  _T_280; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141477.6]
  wire  _T_281; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141478.6]
  wire  _T_282; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141479.6]
  wire  _T_283; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141480.6]
  wire [1:0] _T_284; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141481.6]
  wire [1:0] _T_285; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141482.6]
  wire  _T_286; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141483.6]
  wire  _T_287; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141484.6]
  wire  _T_288; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141485.6]
  wire [1:0] _T_289; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141486.6]
  wire [1:0] _T_290; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141487.6]
  wire [1:0] _T_291; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141488.6]
  wire [2:0] _T_292; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141489.6]
  wire [2:0] _T_293; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141490.6]
  wire [3:0] _T_294; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141491.6]
  wire [3:0] _T_295; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141492.6]
  wire [4:0] _T_296; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141493.6]
  wire [4:0] _T_297; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141494.6]
  wire [5:0] _T_298; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141495.6]
  wire [31:0] _T_301; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141498.6]
  wire [31:0] _T_302; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141499.6]
  wire  _T_303; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141500.6]
  wire [15:0] _T_304; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141501.6]
  wire [15:0] _T_305; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141502.6]
  wire  _T_306; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141503.6]
  wire [7:0] _T_307; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141504.6]
  wire [7:0] _T_308; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141505.6]
  wire  _T_309; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141506.6]
  wire [3:0] _T_310; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141507.6]
  wire [3:0] _T_311; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141508.6]
  wire  _T_312; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141509.6]
  wire  _T_313; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141510.6]
  wire  _T_314; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141511.6]
  wire  _T_315; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141512.6]
  wire [1:0] _T_316; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141513.6]
  wire [1:0] _T_317; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141514.6]
  wire  _T_318; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141515.6]
  wire  _T_319; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141516.6]
  wire  _T_320; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141517.6]
  wire [1:0] _T_321; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141518.6]
  wire [1:0] _T_322; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141519.6]
  wire [1:0] _T_323; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141520.6]
  wire [2:0] _T_324; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141521.6]
  wire [3:0] _T_325; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141522.6]
  wire [3:0] _T_326; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141523.6]
  wire  _T_327; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141524.6]
  wire  _T_328; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141525.6]
  wire  _T_329; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141526.6]
  wire  _T_330; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141527.6]
  wire [1:0] _T_331; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141528.6]
  wire [1:0] _T_332; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141529.6]
  wire  _T_333; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141530.6]
  wire  _T_334; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141531.6]
  wire  _T_335; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141532.6]
  wire [1:0] _T_336; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141533.6]
  wire [1:0] _T_337; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141534.6]
  wire [1:0] _T_338; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141535.6]
  wire [2:0] _T_339; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141536.6]
  wire [2:0] _T_340; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141537.6]
  wire [3:0] _T_341; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141538.6]
  wire [7:0] _T_342; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141539.6]
  wire [7:0] _T_343; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141540.6]
  wire  _T_344; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141541.6]
  wire [3:0] _T_345; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141542.6]
  wire [3:0] _T_346; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141543.6]
  wire  _T_347; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141544.6]
  wire  _T_348; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141545.6]
  wire  _T_349; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141546.6]
  wire  _T_350; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141547.6]
  wire [1:0] _T_351; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141548.6]
  wire [1:0] _T_352; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141549.6]
  wire  _T_353; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141550.6]
  wire  _T_354; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141551.6]
  wire  _T_355; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141552.6]
  wire [1:0] _T_356; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141553.6]
  wire [1:0] _T_357; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141554.6]
  wire [1:0] _T_358; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141555.6]
  wire [2:0] _T_359; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141556.6]
  wire [3:0] _T_360; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141557.6]
  wire [3:0] _T_361; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141558.6]
  wire  _T_362; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141559.6]
  wire  _T_363; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141560.6]
  wire  _T_364; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141561.6]
  wire  _T_365; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141562.6]
  wire [1:0] _T_366; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141563.6]
  wire [1:0] _T_367; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141564.6]
  wire  _T_368; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141565.6]
  wire  _T_369; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141566.6]
  wire  _T_370; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141567.6]
  wire [1:0] _T_371; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141568.6]
  wire [1:0] _T_372; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141569.6]
  wire [1:0] _T_373; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141570.6]
  wire [2:0] _T_374; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141571.6]
  wire [2:0] _T_375; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141572.6]
  wire [3:0] _T_376; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141573.6]
  wire [3:0] _T_377; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141574.6]
  wire [4:0] _T_378; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141575.6]
  wire [15:0] _T_379; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141576.6]
  wire [15:0] _T_380; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141577.6]
  wire  _T_381; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141578.6]
  wire [7:0] _T_382; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141579.6]
  wire [7:0] _T_383; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141580.6]
  wire  _T_384; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141581.6]
  wire [3:0] _T_385; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141582.6]
  wire [3:0] _T_386; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141583.6]
  wire  _T_387; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141584.6]
  wire  _T_388; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141585.6]
  wire  _T_389; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141586.6]
  wire  _T_390; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141587.6]
  wire [1:0] _T_391; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141588.6]
  wire [1:0] _T_392; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141589.6]
  wire  _T_393; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141590.6]
  wire  _T_394; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141591.6]
  wire  _T_395; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141592.6]
  wire [1:0] _T_396; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141593.6]
  wire [1:0] _T_397; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141594.6]
  wire [1:0] _T_398; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141595.6]
  wire [2:0] _T_399; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141596.6]
  wire [3:0] _T_400; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141597.6]
  wire [3:0] _T_401; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141598.6]
  wire  _T_402; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141599.6]
  wire  _T_403; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141600.6]
  wire  _T_404; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141601.6]
  wire  _T_405; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141602.6]
  wire [1:0] _T_406; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141603.6]
  wire [1:0] _T_407; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141604.6]
  wire  _T_408; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141605.6]
  wire  _T_409; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141606.6]
  wire  _T_410; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141607.6]
  wire [1:0] _T_411; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141608.6]
  wire [1:0] _T_412; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141609.6]
  wire [1:0] _T_413; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141610.6]
  wire [2:0] _T_414; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141611.6]
  wire [2:0] _T_415; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141612.6]
  wire [3:0] _T_416; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141613.6]
  wire [7:0] _T_417; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141614.6]
  wire [7:0] _T_418; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141615.6]
  wire  _T_419; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141616.6]
  wire [3:0] _T_420; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141617.6]
  wire [3:0] _T_421; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141618.6]
  wire  _T_422; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141619.6]
  wire  _T_423; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141620.6]
  wire  _T_424; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141621.6]
  wire  _T_425; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141622.6]
  wire [1:0] _T_426; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141623.6]
  wire [1:0] _T_427; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141624.6]
  wire  _T_428; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141625.6]
  wire  _T_429; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141626.6]
  wire  _T_430; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141627.6]
  wire [1:0] _T_431; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141628.6]
  wire [1:0] _T_432; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141629.6]
  wire [1:0] _T_433; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141630.6]
  wire [2:0] _T_434; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141631.6]
  wire [3:0] _T_435; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141632.6]
  wire [3:0] _T_436; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141633.6]
  wire  _T_437; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141634.6]
  wire  _T_438; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141635.6]
  wire  _T_439; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141636.6]
  wire  _T_440; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141637.6]
  wire [1:0] _T_441; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141638.6]
  wire [1:0] _T_442; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141639.6]
  wire  _T_443; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141640.6]
  wire  _T_444; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141641.6]
  wire  _T_445; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141642.6]
  wire [1:0] _T_446; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141643.6]
  wire [1:0] _T_447; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141644.6]
  wire [1:0] _T_448; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141645.6]
  wire [2:0] _T_449; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141646.6]
  wire [2:0] _T_450; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141647.6]
  wire [3:0] _T_451; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141648.6]
  wire [3:0] _T_452; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141649.6]
  wire [4:0] _T_453; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141650.6]
  wire [4:0] _T_454; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141651.6]
  wire [5:0] _T_455; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141652.6]
  wire [6:0] _T_458; // @[Multiplier.scala 152:35:boom.system.TestHarness.BoomConfig.fir@141655.6]
  wire [6:0] _T_459; // @[Multiplier.scala 152:35:boom.system.TestHarness.BoomConfig.fir@141656.6]
  wire [5:0] _T_460; // @[Multiplier.scala 152:35:boom.system.TestHarness.BoomConfig.fir@141657.6]
  wire [5:0] _T_461; // @[Multiplier.scala 152:21:boom.system.TestHarness.BoomConfig.fir@141658.6]
  wire  _T_463; // @[Multiplier.scala 153:33:boom.system.TestHarness.BoomConfig.fir@141660.6]
  wire  _T_464; // @[Multiplier.scala 153:30:boom.system.TestHarness.BoomConfig.fir@141661.6]
  wire  _T_465; // @[Multiplier.scala 153:52:boom.system.TestHarness.BoomConfig.fir@141662.6]
  wire  _T_466; // @[Multiplier.scala 153:41:boom.system.TestHarness.BoomConfig.fir@141663.6]
  wire [126:0] _GEN_39; // @[Multiplier.scala 155:39:boom.system.TestHarness.BoomConfig.fir@141666.8]
  wire [126:0] _T_468; // @[Multiplier.scala 155:39:boom.system.TestHarness.BoomConfig.fir@141666.8]
  wire [128:0] _GEN_16; // @[Multiplier.scala 154:19:boom.system.TestHarness.BoomConfig.fir@141664.6]
  wire  _T_471; // @[Multiplier.scala 159:18:boom.system.TestHarness.BoomConfig.fir@141672.6]
  wire  _T_472; // @[Decoupled.scala 37:37:boom.system.TestHarness.BoomConfig.fir@141677.4]
  wire  _T_473; // @[Multiplier.scala 161:24:boom.system.TestHarness.BoomConfig.fir@141678.4]
  wire  _T_474; // @[Decoupled.scala 37:37:boom.system.TestHarness.BoomConfig.fir@141682.4]
  wire  _T_475; // @[Multiplier.scala 165:46:boom.system.TestHarness.BoomConfig.fir@141684.6]
  wire  _T_480; // @[Multiplier.scala 168:46:boom.system.TestHarness.BoomConfig.fir@141692.6]
  wire [2:0] _T_481; // @[Multiplier.scala 168:38:boom.system.TestHarness.BoomConfig.fir@141693.6]
  wire  _T_482; // @[Multiplier.scala 169:46:boom.system.TestHarness.BoomConfig.fir@141695.6]
  wire [64:0] _T_484; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141698.6]
  wire [2:0] _T_486; // @[Multiplier.scala 175:23:boom.system.TestHarness.BoomConfig.fir@141704.4]
  wire  outMul; // @[Multiplier.scala 175:52:boom.system.TestHarness.BoomConfig.fir@141707.4]
  wire  _T_489; // @[Multiplier.scala 78:62:boom.system.TestHarness.BoomConfig.fir@141708.4]
  wire  _T_492; // @[Multiplier.scala 176:52:boom.system.TestHarness.BoomConfig.fir@141711.4]
  wire [31:0] _T_493; // @[Multiplier.scala 176:69:boom.system.TestHarness.BoomConfig.fir@141712.4]
  wire [31:0] _T_494; // @[Multiplier.scala 176:86:boom.system.TestHarness.BoomConfig.fir@141713.4]
  wire [31:0] loOut; // @[Multiplier.scala 176:18:boom.system.TestHarness.BoomConfig.fir@141714.4]
  wire  _T_497; // @[Multiplier.scala 177:50:boom.system.TestHarness.BoomConfig.fir@141717.4]
  wire [31:0] _T_499; // @[Bitwise.scala 72:12:boom.system.TestHarness.BoomConfig.fir@141719.4]
  wire [31:0] hiOut; // @[Multiplier.scala 177:18:boom.system.TestHarness.BoomConfig.fir@141721.4]
  wire  _T_502; // @[Multiplier.scala 180:27:boom.system.TestHarness.BoomConfig.fir@141725.4]
  wire  _T_503; // @[Multiplier.scala 180:51:boom.system.TestHarness.BoomConfig.fir@141726.4]
  assign _T_22 = io_req_bits_fn & 4'h4; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141179.4]
  assign cmdMul = _T_22 == 4'h0; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141180.4]
  assign _T_25 = io_req_bits_fn & 4'h5; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141182.4]
  assign _T_26 = _T_25 == 4'h1; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141183.4]
  assign _T_27 = io_req_bits_fn & 4'h2; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141184.4]
  assign _T_28 = _T_27 == 4'h2; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141185.4]
  assign cmdHi = _T_26 | _T_28; // @[Decode.scala 15:30:boom.system.TestHarness.BoomConfig.fir@141187.4]
  assign _T_31 = io_req_bits_fn & 4'h6; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141188.4]
  assign _T_32 = _T_31 == 4'h0; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141189.4]
  assign _T_33 = io_req_bits_fn & 4'h1; // @[Decode.scala 14:65:boom.system.TestHarness.BoomConfig.fir@141190.4]
  assign _T_34 = _T_33 == 4'h0; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141191.4]
  assign lhsSigned = _T_32 | _T_34; // @[Decode.scala 15:30:boom.system.TestHarness.BoomConfig.fir@141193.4]
  assign _T_38 = _T_25 == 4'h4; // @[Decode.scala 14:121:boom.system.TestHarness.BoomConfig.fir@141195.4]
  assign rhsSigned = _T_32 | _T_38; // @[Decode.scala 15:30:boom.system.TestHarness.BoomConfig.fir@141197.4]
  assign _T_41 = io_req_bits_dw == 1'h0; // @[Multiplier.scala 78:62:boom.system.TestHarness.BoomConfig.fir@141202.4]
  assign _T_43 = io_req_bits_in1[31]; // @[Multiplier.scala 81:38:boom.system.TestHarness.BoomConfig.fir@141204.4]
  assign _T_44 = io_req_bits_in1[63]; // @[Multiplier.scala 81:48:boom.system.TestHarness.BoomConfig.fir@141205.4]
  assign _T_45 = _T_41 ? _T_43 : _T_44; // @[Multiplier.scala 81:29:boom.system.TestHarness.BoomConfig.fir@141206.4]
  assign lhs_sign = lhsSigned & _T_45; // @[Multiplier.scala 81:23:boom.system.TestHarness.BoomConfig.fir@141207.4]
  assign _T_47 = lhs_sign ? 32'hffffffff : 32'h0; // @[Bitwise.scala 72:12:boom.system.TestHarness.BoomConfig.fir@141209.4]
  assign _T_48 = io_req_bits_in1[63:32]; // @[Multiplier.scala 82:43:boom.system.TestHarness.BoomConfig.fir@141210.4]
  assign _T_49 = _T_41 ? _T_47 : _T_48; // @[Multiplier.scala 82:17:boom.system.TestHarness.BoomConfig.fir@141211.4]
  assign _T_50 = io_req_bits_in1[31:0]; // @[Multiplier.scala 83:15:boom.system.TestHarness.BoomConfig.fir@141212.4]
  assign lhs_in = {_T_49,_T_50}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141213.4]
  assign _T_53 = io_req_bits_in2[31]; // @[Multiplier.scala 81:38:boom.system.TestHarness.BoomConfig.fir@141216.4]
  assign _T_54 = io_req_bits_in2[63]; // @[Multiplier.scala 81:48:boom.system.TestHarness.BoomConfig.fir@141217.4]
  assign _T_55 = _T_41 ? _T_53 : _T_54; // @[Multiplier.scala 81:29:boom.system.TestHarness.BoomConfig.fir@141218.4]
  assign rhs_sign = rhsSigned & _T_55; // @[Multiplier.scala 81:23:boom.system.TestHarness.BoomConfig.fir@141219.4]
  assign _T_57 = rhs_sign ? 32'hffffffff : 32'h0; // @[Bitwise.scala 72:12:boom.system.TestHarness.BoomConfig.fir@141221.4]
  assign _T_58 = io_req_bits_in2[63:32]; // @[Multiplier.scala 82:43:boom.system.TestHarness.BoomConfig.fir@141222.4]
  assign _T_59 = _T_41 ? _T_57 : _T_58; // @[Multiplier.scala 82:17:boom.system.TestHarness.BoomConfig.fir@141223.4]
  assign _T_60 = io_req_bits_in2[31:0]; // @[Multiplier.scala 83:15:boom.system.TestHarness.BoomConfig.fir@141224.4]
  assign rhs_in = {_T_59,_T_60}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141225.4]
  assign _T_61 = remainder[128:64]; // @[Multiplier.scala 88:29:boom.system.TestHarness.BoomConfig.fir@141226.4]
  assign _T_62 = _T_61 - divisor; // @[Multiplier.scala 88:37:boom.system.TestHarness.BoomConfig.fir@141227.4]
  assign _T_63 = $unsigned(_T_62); // @[Multiplier.scala 88:37:boom.system.TestHarness.BoomConfig.fir@141228.4]
  assign subtractor = _T_63[64:0]; // @[Multiplier.scala 88:37:boom.system.TestHarness.BoomConfig.fir@141229.4]
  assign _T_64 = remainder[128:65]; // @[Multiplier.scala 89:36:boom.system.TestHarness.BoomConfig.fir@141230.4]
  assign _T_65 = remainder[63:0]; // @[Multiplier.scala 89:57:boom.system.TestHarness.BoomConfig.fir@141231.4]
  assign result = resHi ? _T_64 : _T_65; // @[Multiplier.scala 89:19:boom.system.TestHarness.BoomConfig.fir@141232.4]
  assign _T_66 = 64'h0 - result; // @[Multiplier.scala 90:27:boom.system.TestHarness.BoomConfig.fir@141233.4]
  assign _T_67 = $unsigned(_T_66); // @[Multiplier.scala 90:27:boom.system.TestHarness.BoomConfig.fir@141234.4]
  assign negated_remainder = _T_67[63:0]; // @[Multiplier.scala 90:27:boom.system.TestHarness.BoomConfig.fir@141235.4]
  assign _T_68 = state == 3'h1; // @[Multiplier.scala 92:39:boom.system.TestHarness.BoomConfig.fir@141236.4]
  assign _T_69 = remainder[63]; // @[Multiplier.scala 93:20:boom.system.TestHarness.BoomConfig.fir@141238.6]
  assign _T_70 = divisor[63]; // @[Multiplier.scala 96:18:boom.system.TestHarness.BoomConfig.fir@141242.6]
  assign _T_71 = state == 3'h5; // @[Multiplier.scala 101:39:boom.system.TestHarness.BoomConfig.fir@141248.4]
  assign _T_72 = state == 3'h2; // @[Multiplier.scala 106:39:boom.system.TestHarness.BoomConfig.fir@141254.4]
  assign _T_73 = remainder[129:65]; // @[Multiplier.scala 107:31:boom.system.TestHarness.BoomConfig.fir@141256.6]
  assign _T_75 = {_T_73,_T_65}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141258.6]
  assign _T_76 = remainder[64]; // @[Multiplier.scala 108:31:boom.system.TestHarness.BoomConfig.fir@141259.6]
  assign _T_77 = _T_75[63:0]; // @[Multiplier.scala 109:24:boom.system.TestHarness.BoomConfig.fir@141260.6]
  assign _T_78 = _T_75[128:64]; // @[Multiplier.scala 110:23:boom.system.TestHarness.BoomConfig.fir@141261.6]
  assign _T_79 = $signed(_T_78); // @[Multiplier.scala 110:37:boom.system.TestHarness.BoomConfig.fir@141262.6]
  assign _T_80 = $signed(divisor); // @[Multiplier.scala 111:26:boom.system.TestHarness.BoomConfig.fir@141263.6]
  assign _T_81 = _T_77[7:0]; // @[Multiplier.scala 112:38:boom.system.TestHarness.BoomConfig.fir@141264.6]
  assign _T_82 = {_T_76,_T_81}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141265.6]
  assign _T_83 = $signed(_T_82); // @[Multiplier.scala 112:60:boom.system.TestHarness.BoomConfig.fir@141266.6]
  assign _GEN_37 = {{56{_T_83[8]}},_T_83}; // @[Multiplier.scala 112:67:boom.system.TestHarness.BoomConfig.fir@141267.6]
  assign _T_84 = $signed(_GEN_37) * $signed(_T_80); // @[Multiplier.scala 112:67:boom.system.TestHarness.BoomConfig.fir@141267.6]
  assign _GEN_38 = {{9{_T_79[64]}},_T_79}; // @[Multiplier.scala 112:76:boom.system.TestHarness.BoomConfig.fir@141268.6]
  assign _T_86 = $signed(_T_84) + $signed(_GEN_38); // @[Multiplier.scala 112:76:boom.system.TestHarness.BoomConfig.fir@141269.6]
  assign _T_87 = $signed(_T_86); // @[Multiplier.scala 112:76:boom.system.TestHarness.BoomConfig.fir@141270.6]
  assign _T_88 = _T_77[63:8]; // @[Multiplier.scala 113:38:boom.system.TestHarness.BoomConfig.fir@141271.6]
  assign _T_89 = $unsigned(_T_87); // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141272.6]
  assign _T_90 = {_T_89,_T_88}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141273.6]
  assign _T_91 = count == 7'h6; // @[Multiplier.scala 114:32:boom.system.TestHarness.BoomConfig.fir@141274.6]
  assign _T_92 = _T_91 & neg_out; // @[Multiplier.scala 114:57:boom.system.TestHarness.BoomConfig.fir@141275.6]
  assign _T_93 = count * 7'h8; // @[Multiplier.scala 116:56:boom.system.TestHarness.BoomConfig.fir@141276.6]
  assign _T_94 = _T_93[5:0]; // @[Multiplier.scala 116:72:boom.system.TestHarness.BoomConfig.fir@141277.6]
  assign _T_95 = $signed(-65'sh10000000000000000) >>> _T_94; // @[Multiplier.scala 116:46:boom.system.TestHarness.BoomConfig.fir@141278.6]
  assign _T_96 = _T_95[63:0]; // @[Multiplier.scala 116:91:boom.system.TestHarness.BoomConfig.fir@141279.6]
  assign _T_97 = count != 7'h7; // @[Multiplier.scala 117:47:boom.system.TestHarness.BoomConfig.fir@141280.6]
  assign _T_99 = count != 7'h0; // @[Multiplier.scala 117:81:boom.system.TestHarness.BoomConfig.fir@141282.6]
  assign _T_100 = _T_97 & _T_99; // @[Multiplier.scala 117:72:boom.system.TestHarness.BoomConfig.fir@141283.6]
  assign _T_101 = isHi == 1'h0; // @[Multiplier.scala 118:7:boom.system.TestHarness.BoomConfig.fir@141284.6]
  assign _T_102 = _T_100 & _T_101; // @[Multiplier.scala 117:87:boom.system.TestHarness.BoomConfig.fir@141285.6]
  assign _T_103 = ~ _T_96; // @[Multiplier.scala 118:26:boom.system.TestHarness.BoomConfig.fir@141286.6]
  assign _T_104 = _T_77 & _T_103; // @[Multiplier.scala 118:24:boom.system.TestHarness.BoomConfig.fir@141287.6]
  assign _T_105 = _T_104 == 64'h0; // @[Multiplier.scala 118:37:boom.system.TestHarness.BoomConfig.fir@141288.6]
  assign _T_106 = _T_102 & _T_105; // @[Multiplier.scala 118:13:boom.system.TestHarness.BoomConfig.fir@141289.6]
  assign _T_108 = 11'h40 - _T_93; // @[Multiplier.scala 119:36:boom.system.TestHarness.BoomConfig.fir@141291.6]
  assign _T_109 = $unsigned(_T_108); // @[Multiplier.scala 119:36:boom.system.TestHarness.BoomConfig.fir@141292.6]
  assign _T_110 = _T_109[10:0]; // @[Multiplier.scala 119:36:boom.system.TestHarness.BoomConfig.fir@141293.6]
  assign _T_111 = _T_110[5:0]; // @[Multiplier.scala 119:60:boom.system.TestHarness.BoomConfig.fir@141294.6]
  assign _T_112 = _T_75 >> _T_111; // @[Multiplier.scala 119:27:boom.system.TestHarness.BoomConfig.fir@141295.6]
  assign _T_113 = _T_90[128:64]; // @[Multiplier.scala 120:37:boom.system.TestHarness.BoomConfig.fir@141296.6]
  assign _T_114 = _T_106 ? {{1'd0}, _T_112} : _T_90; // @[Multiplier.scala 120:55:boom.system.TestHarness.BoomConfig.fir@141297.6]
  assign _T_115 = _T_114[63:0]; // @[Multiplier.scala 120:82:boom.system.TestHarness.BoomConfig.fir@141298.6]
  assign _T_116 = {_T_113,_T_115}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141299.6]
  assign _T_117 = _T_116[128:64]; // @[Multiplier.scala 121:34:boom.system.TestHarness.BoomConfig.fir@141300.6]
  assign _T_118 = _T_116[63:0]; // @[Multiplier.scala 121:67:boom.system.TestHarness.BoomConfig.fir@141301.6]
  assign _T_119 = {_T_117,_T_92}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141302.6]
  assign _T_120 = {_T_119,_T_118}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141303.6]
  assign _T_122 = count + 7'h1; // @[Multiplier.scala 123:20:boom.system.TestHarness.BoomConfig.fir@141306.6]
  assign _T_123 = count == 7'h7; // @[Multiplier.scala 124:25:boom.system.TestHarness.BoomConfig.fir@141308.6]
  assign _T_124 = _T_106 | _T_123; // @[Multiplier.scala 124:16:boom.system.TestHarness.BoomConfig.fir@141309.6]
  assign _T_125 = state == 3'h3; // @[Multiplier.scala 129:39:boom.system.TestHarness.BoomConfig.fir@141315.4]
  assign _T_126 = subtractor[64]; // @[Multiplier.scala 133:28:boom.system.TestHarness.BoomConfig.fir@141317.6]
  assign _T_127 = remainder[127:64]; // @[Multiplier.scala 134:24:boom.system.TestHarness.BoomConfig.fir@141318.6]
  assign _T_128 = subtractor[63:0]; // @[Multiplier.scala 134:45:boom.system.TestHarness.BoomConfig.fir@141319.6]
  assign _T_129 = _T_126 ? _T_127 : _T_128; // @[Multiplier.scala 134:14:boom.system.TestHarness.BoomConfig.fir@141320.6]
  assign _T_131 = _T_126 == 1'h0; // @[Multiplier.scala 134:67:boom.system.TestHarness.BoomConfig.fir@141322.6]
  assign _T_132 = {_T_129,_T_65}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141323.6]
  assign _T_133 = {_T_132,_T_131}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141324.6]
  assign _T_134 = count == 7'h40; // @[Multiplier.scala 138:17:boom.system.TestHarness.BoomConfig.fir@141326.6]
  assign _T_138 = count == 7'h0; // @[Multiplier.scala 146:24:boom.system.TestHarness.BoomConfig.fir@141335.6]
  assign _T_141 = _T_138 & _T_131; // @[Multiplier.scala 146:30:boom.system.TestHarness.BoomConfig.fir@141338.6]
  assign _T_143 = divisor[63:0]; // @[Multiplier.scala 150:36:boom.system.TestHarness.BoomConfig.fir@141340.6]
  assign _T_144 = _T_143[63:32]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141341.6]
  assign _T_145 = _T_143[31:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141342.6]
  assign _T_146 = _T_144 != 32'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141343.6]
  assign _T_147 = _T_144[31:16]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141344.6]
  assign _T_148 = _T_144[15:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141345.6]
  assign _T_149 = _T_147 != 16'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141346.6]
  assign _T_150 = _T_147[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141347.6]
  assign _T_151 = _T_147[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141348.6]
  assign _T_152 = _T_150 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141349.6]
  assign _T_153 = _T_150[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141350.6]
  assign _T_154 = _T_150[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141351.6]
  assign _T_155 = _T_153 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141352.6]
  assign _T_156 = _T_153[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141353.6]
  assign _T_157 = _T_153[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141354.6]
  assign _T_158 = _T_153[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141355.6]
  assign _T_159 = _T_157 ? 2'h2 : {{1'd0}, _T_158}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141356.6]
  assign _T_160 = _T_156 ? 2'h3 : _T_159; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141357.6]
  assign _T_161 = _T_154[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141358.6]
  assign _T_162 = _T_154[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141359.6]
  assign _T_163 = _T_154[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141360.6]
  assign _T_164 = _T_162 ? 2'h2 : {{1'd0}, _T_163}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141361.6]
  assign _T_165 = _T_161 ? 2'h3 : _T_164; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141362.6]
  assign _T_166 = _T_155 ? _T_160 : _T_165; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141363.6]
  assign _T_167 = {_T_155,_T_166}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141364.6]
  assign _T_168 = _T_151[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141365.6]
  assign _T_169 = _T_151[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141366.6]
  assign _T_170 = _T_168 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141367.6]
  assign _T_171 = _T_168[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141368.6]
  assign _T_172 = _T_168[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141369.6]
  assign _T_173 = _T_168[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141370.6]
  assign _T_174 = _T_172 ? 2'h2 : {{1'd0}, _T_173}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141371.6]
  assign _T_175 = _T_171 ? 2'h3 : _T_174; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141372.6]
  assign _T_176 = _T_169[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141373.6]
  assign _T_177 = _T_169[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141374.6]
  assign _T_178 = _T_169[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141375.6]
  assign _T_179 = _T_177 ? 2'h2 : {{1'd0}, _T_178}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141376.6]
  assign _T_180 = _T_176 ? 2'h3 : _T_179; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141377.6]
  assign _T_181 = _T_170 ? _T_175 : _T_180; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141378.6]
  assign _T_182 = {_T_170,_T_181}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141379.6]
  assign _T_183 = _T_152 ? _T_167 : _T_182; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141380.6]
  assign _T_184 = {_T_152,_T_183}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141381.6]
  assign _T_185 = _T_148[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141382.6]
  assign _T_186 = _T_148[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141383.6]
  assign _T_187 = _T_185 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141384.6]
  assign _T_188 = _T_185[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141385.6]
  assign _T_189 = _T_185[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141386.6]
  assign _T_190 = _T_188 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141387.6]
  assign _T_191 = _T_188[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141388.6]
  assign _T_192 = _T_188[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141389.6]
  assign _T_193 = _T_188[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141390.6]
  assign _T_194 = _T_192 ? 2'h2 : {{1'd0}, _T_193}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141391.6]
  assign _T_195 = _T_191 ? 2'h3 : _T_194; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141392.6]
  assign _T_196 = _T_189[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141393.6]
  assign _T_197 = _T_189[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141394.6]
  assign _T_198 = _T_189[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141395.6]
  assign _T_199 = _T_197 ? 2'h2 : {{1'd0}, _T_198}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141396.6]
  assign _T_200 = _T_196 ? 2'h3 : _T_199; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141397.6]
  assign _T_201 = _T_190 ? _T_195 : _T_200; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141398.6]
  assign _T_202 = {_T_190,_T_201}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141399.6]
  assign _T_203 = _T_186[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141400.6]
  assign _T_204 = _T_186[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141401.6]
  assign _T_205 = _T_203 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141402.6]
  assign _T_206 = _T_203[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141403.6]
  assign _T_207 = _T_203[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141404.6]
  assign _T_208 = _T_203[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141405.6]
  assign _T_209 = _T_207 ? 2'h2 : {{1'd0}, _T_208}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141406.6]
  assign _T_210 = _T_206 ? 2'h3 : _T_209; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141407.6]
  assign _T_211 = _T_204[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141408.6]
  assign _T_212 = _T_204[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141409.6]
  assign _T_213 = _T_204[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141410.6]
  assign _T_214 = _T_212 ? 2'h2 : {{1'd0}, _T_213}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141411.6]
  assign _T_215 = _T_211 ? 2'h3 : _T_214; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141412.6]
  assign _T_216 = _T_205 ? _T_210 : _T_215; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141413.6]
  assign _T_217 = {_T_205,_T_216}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141414.6]
  assign _T_218 = _T_187 ? _T_202 : _T_217; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141415.6]
  assign _T_219 = {_T_187,_T_218}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141416.6]
  assign _T_220 = _T_149 ? _T_184 : _T_219; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141417.6]
  assign _T_221 = {_T_149,_T_220}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141418.6]
  assign _T_222 = _T_145[31:16]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141419.6]
  assign _T_223 = _T_145[15:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141420.6]
  assign _T_224 = _T_222 != 16'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141421.6]
  assign _T_225 = _T_222[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141422.6]
  assign _T_226 = _T_222[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141423.6]
  assign _T_227 = _T_225 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141424.6]
  assign _T_228 = _T_225[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141425.6]
  assign _T_229 = _T_225[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141426.6]
  assign _T_230 = _T_228 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141427.6]
  assign _T_231 = _T_228[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141428.6]
  assign _T_232 = _T_228[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141429.6]
  assign _T_233 = _T_228[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141430.6]
  assign _T_234 = _T_232 ? 2'h2 : {{1'd0}, _T_233}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141431.6]
  assign _T_235 = _T_231 ? 2'h3 : _T_234; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141432.6]
  assign _T_236 = _T_229[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141433.6]
  assign _T_237 = _T_229[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141434.6]
  assign _T_238 = _T_229[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141435.6]
  assign _T_239 = _T_237 ? 2'h2 : {{1'd0}, _T_238}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141436.6]
  assign _T_240 = _T_236 ? 2'h3 : _T_239; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141437.6]
  assign _T_241 = _T_230 ? _T_235 : _T_240; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141438.6]
  assign _T_242 = {_T_230,_T_241}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141439.6]
  assign _T_243 = _T_226[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141440.6]
  assign _T_244 = _T_226[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141441.6]
  assign _T_245 = _T_243 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141442.6]
  assign _T_246 = _T_243[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141443.6]
  assign _T_247 = _T_243[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141444.6]
  assign _T_248 = _T_243[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141445.6]
  assign _T_249 = _T_247 ? 2'h2 : {{1'd0}, _T_248}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141446.6]
  assign _T_250 = _T_246 ? 2'h3 : _T_249; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141447.6]
  assign _T_251 = _T_244[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141448.6]
  assign _T_252 = _T_244[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141449.6]
  assign _T_253 = _T_244[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141450.6]
  assign _T_254 = _T_252 ? 2'h2 : {{1'd0}, _T_253}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141451.6]
  assign _T_255 = _T_251 ? 2'h3 : _T_254; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141452.6]
  assign _T_256 = _T_245 ? _T_250 : _T_255; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141453.6]
  assign _T_257 = {_T_245,_T_256}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141454.6]
  assign _T_258 = _T_227 ? _T_242 : _T_257; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141455.6]
  assign _T_259 = {_T_227,_T_258}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141456.6]
  assign _T_260 = _T_223[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141457.6]
  assign _T_261 = _T_223[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141458.6]
  assign _T_262 = _T_260 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141459.6]
  assign _T_263 = _T_260[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141460.6]
  assign _T_264 = _T_260[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141461.6]
  assign _T_265 = _T_263 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141462.6]
  assign _T_266 = _T_263[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141463.6]
  assign _T_267 = _T_263[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141464.6]
  assign _T_268 = _T_263[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141465.6]
  assign _T_269 = _T_267 ? 2'h2 : {{1'd0}, _T_268}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141466.6]
  assign _T_270 = _T_266 ? 2'h3 : _T_269; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141467.6]
  assign _T_271 = _T_264[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141468.6]
  assign _T_272 = _T_264[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141469.6]
  assign _T_273 = _T_264[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141470.6]
  assign _T_274 = _T_272 ? 2'h2 : {{1'd0}, _T_273}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141471.6]
  assign _T_275 = _T_271 ? 2'h3 : _T_274; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141472.6]
  assign _T_276 = _T_265 ? _T_270 : _T_275; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141473.6]
  assign _T_277 = {_T_265,_T_276}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141474.6]
  assign _T_278 = _T_261[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141475.6]
  assign _T_279 = _T_261[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141476.6]
  assign _T_280 = _T_278 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141477.6]
  assign _T_281 = _T_278[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141478.6]
  assign _T_282 = _T_278[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141479.6]
  assign _T_283 = _T_278[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141480.6]
  assign _T_284 = _T_282 ? 2'h2 : {{1'd0}, _T_283}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141481.6]
  assign _T_285 = _T_281 ? 2'h3 : _T_284; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141482.6]
  assign _T_286 = _T_279[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141483.6]
  assign _T_287 = _T_279[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141484.6]
  assign _T_288 = _T_279[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141485.6]
  assign _T_289 = _T_287 ? 2'h2 : {{1'd0}, _T_288}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141486.6]
  assign _T_290 = _T_286 ? 2'h3 : _T_289; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141487.6]
  assign _T_291 = _T_280 ? _T_285 : _T_290; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141488.6]
  assign _T_292 = {_T_280,_T_291}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141489.6]
  assign _T_293 = _T_262 ? _T_277 : _T_292; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141490.6]
  assign _T_294 = {_T_262,_T_293}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141491.6]
  assign _T_295 = _T_224 ? _T_259 : _T_294; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141492.6]
  assign _T_296 = {_T_224,_T_295}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141493.6]
  assign _T_297 = _T_146 ? _T_221 : _T_296; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141494.6]
  assign _T_298 = {_T_146,_T_297}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141495.6]
  assign _T_301 = _T_65[63:32]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141498.6]
  assign _T_302 = _T_65[31:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141499.6]
  assign _T_303 = _T_301 != 32'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141500.6]
  assign _T_304 = _T_301[31:16]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141501.6]
  assign _T_305 = _T_301[15:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141502.6]
  assign _T_306 = _T_304 != 16'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141503.6]
  assign _T_307 = _T_304[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141504.6]
  assign _T_308 = _T_304[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141505.6]
  assign _T_309 = _T_307 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141506.6]
  assign _T_310 = _T_307[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141507.6]
  assign _T_311 = _T_307[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141508.6]
  assign _T_312 = _T_310 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141509.6]
  assign _T_313 = _T_310[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141510.6]
  assign _T_314 = _T_310[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141511.6]
  assign _T_315 = _T_310[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141512.6]
  assign _T_316 = _T_314 ? 2'h2 : {{1'd0}, _T_315}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141513.6]
  assign _T_317 = _T_313 ? 2'h3 : _T_316; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141514.6]
  assign _T_318 = _T_311[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141515.6]
  assign _T_319 = _T_311[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141516.6]
  assign _T_320 = _T_311[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141517.6]
  assign _T_321 = _T_319 ? 2'h2 : {{1'd0}, _T_320}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141518.6]
  assign _T_322 = _T_318 ? 2'h3 : _T_321; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141519.6]
  assign _T_323 = _T_312 ? _T_317 : _T_322; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141520.6]
  assign _T_324 = {_T_312,_T_323}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141521.6]
  assign _T_325 = _T_308[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141522.6]
  assign _T_326 = _T_308[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141523.6]
  assign _T_327 = _T_325 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141524.6]
  assign _T_328 = _T_325[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141525.6]
  assign _T_329 = _T_325[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141526.6]
  assign _T_330 = _T_325[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141527.6]
  assign _T_331 = _T_329 ? 2'h2 : {{1'd0}, _T_330}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141528.6]
  assign _T_332 = _T_328 ? 2'h3 : _T_331; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141529.6]
  assign _T_333 = _T_326[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141530.6]
  assign _T_334 = _T_326[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141531.6]
  assign _T_335 = _T_326[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141532.6]
  assign _T_336 = _T_334 ? 2'h2 : {{1'd0}, _T_335}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141533.6]
  assign _T_337 = _T_333 ? 2'h3 : _T_336; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141534.6]
  assign _T_338 = _T_327 ? _T_332 : _T_337; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141535.6]
  assign _T_339 = {_T_327,_T_338}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141536.6]
  assign _T_340 = _T_309 ? _T_324 : _T_339; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141537.6]
  assign _T_341 = {_T_309,_T_340}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141538.6]
  assign _T_342 = _T_305[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141539.6]
  assign _T_343 = _T_305[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141540.6]
  assign _T_344 = _T_342 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141541.6]
  assign _T_345 = _T_342[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141542.6]
  assign _T_346 = _T_342[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141543.6]
  assign _T_347 = _T_345 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141544.6]
  assign _T_348 = _T_345[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141545.6]
  assign _T_349 = _T_345[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141546.6]
  assign _T_350 = _T_345[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141547.6]
  assign _T_351 = _T_349 ? 2'h2 : {{1'd0}, _T_350}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141548.6]
  assign _T_352 = _T_348 ? 2'h3 : _T_351; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141549.6]
  assign _T_353 = _T_346[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141550.6]
  assign _T_354 = _T_346[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141551.6]
  assign _T_355 = _T_346[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141552.6]
  assign _T_356 = _T_354 ? 2'h2 : {{1'd0}, _T_355}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141553.6]
  assign _T_357 = _T_353 ? 2'h3 : _T_356; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141554.6]
  assign _T_358 = _T_347 ? _T_352 : _T_357; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141555.6]
  assign _T_359 = {_T_347,_T_358}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141556.6]
  assign _T_360 = _T_343[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141557.6]
  assign _T_361 = _T_343[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141558.6]
  assign _T_362 = _T_360 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141559.6]
  assign _T_363 = _T_360[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141560.6]
  assign _T_364 = _T_360[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141561.6]
  assign _T_365 = _T_360[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141562.6]
  assign _T_366 = _T_364 ? 2'h2 : {{1'd0}, _T_365}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141563.6]
  assign _T_367 = _T_363 ? 2'h3 : _T_366; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141564.6]
  assign _T_368 = _T_361[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141565.6]
  assign _T_369 = _T_361[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141566.6]
  assign _T_370 = _T_361[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141567.6]
  assign _T_371 = _T_369 ? 2'h2 : {{1'd0}, _T_370}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141568.6]
  assign _T_372 = _T_368 ? 2'h3 : _T_371; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141569.6]
  assign _T_373 = _T_362 ? _T_367 : _T_372; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141570.6]
  assign _T_374 = {_T_362,_T_373}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141571.6]
  assign _T_375 = _T_344 ? _T_359 : _T_374; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141572.6]
  assign _T_376 = {_T_344,_T_375}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141573.6]
  assign _T_377 = _T_306 ? _T_341 : _T_376; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141574.6]
  assign _T_378 = {_T_306,_T_377}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141575.6]
  assign _T_379 = _T_302[31:16]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141576.6]
  assign _T_380 = _T_302[15:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141577.6]
  assign _T_381 = _T_379 != 16'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141578.6]
  assign _T_382 = _T_379[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141579.6]
  assign _T_383 = _T_379[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141580.6]
  assign _T_384 = _T_382 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141581.6]
  assign _T_385 = _T_382[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141582.6]
  assign _T_386 = _T_382[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141583.6]
  assign _T_387 = _T_385 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141584.6]
  assign _T_388 = _T_385[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141585.6]
  assign _T_389 = _T_385[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141586.6]
  assign _T_390 = _T_385[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141587.6]
  assign _T_391 = _T_389 ? 2'h2 : {{1'd0}, _T_390}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141588.6]
  assign _T_392 = _T_388 ? 2'h3 : _T_391; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141589.6]
  assign _T_393 = _T_386[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141590.6]
  assign _T_394 = _T_386[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141591.6]
  assign _T_395 = _T_386[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141592.6]
  assign _T_396 = _T_394 ? 2'h2 : {{1'd0}, _T_395}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141593.6]
  assign _T_397 = _T_393 ? 2'h3 : _T_396; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141594.6]
  assign _T_398 = _T_387 ? _T_392 : _T_397; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141595.6]
  assign _T_399 = {_T_387,_T_398}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141596.6]
  assign _T_400 = _T_383[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141597.6]
  assign _T_401 = _T_383[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141598.6]
  assign _T_402 = _T_400 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141599.6]
  assign _T_403 = _T_400[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141600.6]
  assign _T_404 = _T_400[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141601.6]
  assign _T_405 = _T_400[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141602.6]
  assign _T_406 = _T_404 ? 2'h2 : {{1'd0}, _T_405}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141603.6]
  assign _T_407 = _T_403 ? 2'h3 : _T_406; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141604.6]
  assign _T_408 = _T_401[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141605.6]
  assign _T_409 = _T_401[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141606.6]
  assign _T_410 = _T_401[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141607.6]
  assign _T_411 = _T_409 ? 2'h2 : {{1'd0}, _T_410}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141608.6]
  assign _T_412 = _T_408 ? 2'h3 : _T_411; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141609.6]
  assign _T_413 = _T_402 ? _T_407 : _T_412; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141610.6]
  assign _T_414 = {_T_402,_T_413}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141611.6]
  assign _T_415 = _T_384 ? _T_399 : _T_414; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141612.6]
  assign _T_416 = {_T_384,_T_415}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141613.6]
  assign _T_417 = _T_380[15:8]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141614.6]
  assign _T_418 = _T_380[7:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141615.6]
  assign _T_419 = _T_417 != 8'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141616.6]
  assign _T_420 = _T_417[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141617.6]
  assign _T_421 = _T_417[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141618.6]
  assign _T_422 = _T_420 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141619.6]
  assign _T_423 = _T_420[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141620.6]
  assign _T_424 = _T_420[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141621.6]
  assign _T_425 = _T_420[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141622.6]
  assign _T_426 = _T_424 ? 2'h2 : {{1'd0}, _T_425}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141623.6]
  assign _T_427 = _T_423 ? 2'h3 : _T_426; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141624.6]
  assign _T_428 = _T_421[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141625.6]
  assign _T_429 = _T_421[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141626.6]
  assign _T_430 = _T_421[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141627.6]
  assign _T_431 = _T_429 ? 2'h2 : {{1'd0}, _T_430}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141628.6]
  assign _T_432 = _T_428 ? 2'h3 : _T_431; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141629.6]
  assign _T_433 = _T_422 ? _T_427 : _T_432; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141630.6]
  assign _T_434 = {_T_422,_T_433}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141631.6]
  assign _T_435 = _T_418[7:4]; // @[CircuitMath.scala 35:17:boom.system.TestHarness.BoomConfig.fir@141632.6]
  assign _T_436 = _T_418[3:0]; // @[CircuitMath.scala 36:17:boom.system.TestHarness.BoomConfig.fir@141633.6]
  assign _T_437 = _T_435 != 4'h0; // @[CircuitMath.scala 37:22:boom.system.TestHarness.BoomConfig.fir@141634.6]
  assign _T_438 = _T_435[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141635.6]
  assign _T_439 = _T_435[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141636.6]
  assign _T_440 = _T_435[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141637.6]
  assign _T_441 = _T_439 ? 2'h2 : {{1'd0}, _T_440}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141638.6]
  assign _T_442 = _T_438 ? 2'h3 : _T_441; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141639.6]
  assign _T_443 = _T_436[3]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141640.6]
  assign _T_444 = _T_436[2]; // @[CircuitMath.scala 32:12:boom.system.TestHarness.BoomConfig.fir@141641.6]
  assign _T_445 = _T_436[1]; // @[CircuitMath.scala 30:8:boom.system.TestHarness.BoomConfig.fir@141642.6]
  assign _T_446 = _T_444 ? 2'h2 : {{1'd0}, _T_445}; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141643.6]
  assign _T_447 = _T_443 ? 2'h3 : _T_446; // @[CircuitMath.scala 32:10:boom.system.TestHarness.BoomConfig.fir@141644.6]
  assign _T_448 = _T_437 ? _T_442 : _T_447; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141645.6]
  assign _T_449 = {_T_437,_T_448}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141646.6]
  assign _T_450 = _T_419 ? _T_434 : _T_449; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141647.6]
  assign _T_451 = {_T_419,_T_450}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141648.6]
  assign _T_452 = _T_381 ? _T_416 : _T_451; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141649.6]
  assign _T_453 = {_T_381,_T_452}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141650.6]
  assign _T_454 = _T_303 ? _T_378 : _T_453; // @[CircuitMath.scala 38:21:boom.system.TestHarness.BoomConfig.fir@141651.6]
  assign _T_455 = {_T_303,_T_454}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141652.6]
  assign _T_458 = _T_455 - _T_298; // @[Multiplier.scala 152:35:boom.system.TestHarness.BoomConfig.fir@141655.6]
  assign _T_459 = $unsigned(_T_458); // @[Multiplier.scala 152:35:boom.system.TestHarness.BoomConfig.fir@141656.6]
  assign _T_460 = _T_459[5:0]; // @[Multiplier.scala 152:35:boom.system.TestHarness.BoomConfig.fir@141657.6]
  assign _T_461 = ~ _T_460; // @[Multiplier.scala 152:21:boom.system.TestHarness.BoomConfig.fir@141658.6]
  assign _T_463 = _T_141 == 1'h0; // @[Multiplier.scala 153:33:boom.system.TestHarness.BoomConfig.fir@141660.6]
  assign _T_464 = _T_138 & _T_463; // @[Multiplier.scala 153:30:boom.system.TestHarness.BoomConfig.fir@141661.6]
  assign _T_465 = _T_461 >= 6'h1; // @[Multiplier.scala 153:52:boom.system.TestHarness.BoomConfig.fir@141662.6]
  assign _T_466 = _T_464 & _T_465; // @[Multiplier.scala 153:41:boom.system.TestHarness.BoomConfig.fir@141663.6]
  assign _GEN_39 = {{63'd0}, _T_65}; // @[Multiplier.scala 155:39:boom.system.TestHarness.BoomConfig.fir@141666.8]
  assign _T_468 = _GEN_39 << _T_461; // @[Multiplier.scala 155:39:boom.system.TestHarness.BoomConfig.fir@141666.8]
  assign _GEN_16 = _T_466 ? {{2'd0}, _T_468} : _T_133; // @[Multiplier.scala 154:19:boom.system.TestHarness.BoomConfig.fir@141664.6]
  assign _T_471 = _T_141 & _T_101; // @[Multiplier.scala 159:18:boom.system.TestHarness.BoomConfig.fir@141672.6]
  assign _T_472 = io_resp_ready & io_resp_valid; // @[Decoupled.scala 37:37:boom.system.TestHarness.BoomConfig.fir@141677.4]
  assign _T_473 = _T_472 | io_kill; // @[Multiplier.scala 161:24:boom.system.TestHarness.BoomConfig.fir@141678.4]
  assign _T_474 = io_req_ready & io_req_valid; // @[Decoupled.scala 37:37:boom.system.TestHarness.BoomConfig.fir@141682.4]
  assign _T_475 = lhs_sign | rhs_sign; // @[Multiplier.scala 165:46:boom.system.TestHarness.BoomConfig.fir@141684.6]
  assign _T_480 = cmdMul & _T_41; // @[Multiplier.scala 168:46:boom.system.TestHarness.BoomConfig.fir@141692.6]
  assign _T_481 = _T_480 ? 3'h4 : 3'h0; // @[Multiplier.scala 168:38:boom.system.TestHarness.BoomConfig.fir@141693.6]
  assign _T_482 = lhs_sign != rhs_sign; // @[Multiplier.scala 169:46:boom.system.TestHarness.BoomConfig.fir@141695.6]
  assign _T_484 = {rhs_sign,rhs_in}; // @[Cat.scala 30:58:boom.system.TestHarness.BoomConfig.fir@141698.6]
  assign _T_486 = state & 3'h1; // @[Multiplier.scala 175:23:boom.system.TestHarness.BoomConfig.fir@141704.4]
  assign outMul = _T_486 == 3'h0; // @[Multiplier.scala 175:52:boom.system.TestHarness.BoomConfig.fir@141707.4]
  assign _T_489 = req_dw == 1'h0; // @[Multiplier.scala 78:62:boom.system.TestHarness.BoomConfig.fir@141708.4]
  assign _T_492 = _T_489 & outMul; // @[Multiplier.scala 176:52:boom.system.TestHarness.BoomConfig.fir@141711.4]
  assign _T_493 = result[63:32]; // @[Multiplier.scala 176:69:boom.system.TestHarness.BoomConfig.fir@141712.4]
  assign _T_494 = result[31:0]; // @[Multiplier.scala 176:86:boom.system.TestHarness.BoomConfig.fir@141713.4]
  assign loOut = _T_492 ? _T_493 : _T_494; // @[Multiplier.scala 176:18:boom.system.TestHarness.BoomConfig.fir@141714.4]
  assign _T_497 = loOut[31]; // @[Multiplier.scala 177:50:boom.system.TestHarness.BoomConfig.fir@141717.4]
  assign _T_499 = _T_497 ? 32'hffffffff : 32'h0; // @[Bitwise.scala 72:12:boom.system.TestHarness.BoomConfig.fir@141719.4]
  assign hiOut = _T_489 ? _T_499 : _T_493; // @[Multiplier.scala 177:18:boom.system.TestHarness.BoomConfig.fir@141721.4]
  assign _T_502 = state == 3'h6; // @[Multiplier.scala 180:27:boom.system.TestHarness.BoomConfig.fir@141725.4]
  assign _T_503 = state == 3'h7; // @[Multiplier.scala 180:51:boom.system.TestHarness.BoomConfig.fir@141726.4]
  assign io_req_ready = state == 3'h0; // @[Multiplier.scala 181:16:boom.system.TestHarness.BoomConfig.fir@141730.4]
  assign io_resp_valid = _T_502 | _T_503; // @[Multiplier.scala 180:17:boom.system.TestHarness.BoomConfig.fir@141728.4]
  assign io_resp_bits_data = {hiOut,loOut}; // @[Multiplier.scala 179:21:boom.system.TestHarness.BoomConfig.fir@141724.4]
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE
  integer initvar;
  initial begin
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
  `ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  state = _RAND_0[2:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  req_dw = _RAND_1[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_2 = {1{`RANDOM}};
  count = _RAND_2[6:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_3 = {1{`RANDOM}};
  neg_out = _RAND_3[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_4 = {1{`RANDOM}};
  isHi = _RAND_4[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_5 = {1{`RANDOM}};
  resHi = _RAND_5[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_6 = {3{`RANDOM}};
  divisor = _RAND_6[64:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_7 = {5{`RANDOM}};
  remainder = _RAND_7[129:0];
  `endif // RANDOMIZE_REG_INIT
  end
`endif // RANDOMIZE
  always @(posedge clock) begin
    if (reset) begin
      state <= 3'h0;
    end else begin
      if (_T_474) begin
        if (cmdMul) begin
          state <= 3'h2;
        end else begin
          if (_T_475) begin
            state <= 3'h1;
          end else begin
            state <= 3'h3;
          end
        end
      end else begin
        if (_T_473) begin
          state <= 3'h0;
        end else begin
          if (_T_125) begin
            if (_T_134) begin
              if (neg_out) begin
                state <= 3'h5;
              end else begin
                state <= 3'h7;
              end
            end else begin
              if (_T_72) begin
                if (_T_124) begin
                  state <= 3'h6;
                end else begin
                  if (_T_71) begin
                    state <= 3'h7;
                  end else begin
                    if (_T_68) begin
                      state <= 3'h3;
                    end
                  end
                end
              end else begin
                if (_T_71) begin
                  state <= 3'h7;
                end else begin
                  if (_T_68) begin
                    state <= 3'h3;
                  end
                end
              end
            end
          end else begin
            if (_T_72) begin
              if (_T_124) begin
                state <= 3'h6;
              end else begin
                if (_T_71) begin
                  state <= 3'h7;
                end else begin
                  if (_T_68) begin
                    state <= 3'h3;
                  end
                end
              end
            end else begin
              if (_T_71) begin
                state <= 3'h7;
              end else begin
                if (_T_68) begin
                  state <= 3'h3;
                end
              end
            end
          end
        end
      end
    end
    if (_T_474) begin
      req_dw <= io_req_bits_dw;
    end
    if (_T_474) begin
      count <= {{4'd0}, _T_481};
    end else begin
      if (_T_125) begin
        if (_T_466) begin
          count <= {{1'd0}, _T_461};
        end else begin
          count <= _T_122;
        end
      end else begin
        if (_T_72) begin
          count <= _T_122;
        end
      end
    end
    if (_T_474) begin
      if (cmdHi) begin
        neg_out <= lhs_sign;
      end else begin
        neg_out <= _T_482;
      end
    end else begin
      if (_T_125) begin
        if (_T_471) begin
          neg_out <= 1'h0;
        end
      end
    end
    if (_T_474) begin
      isHi <= cmdHi;
    end
    if (_T_474) begin
      resHi <= 1'h0;
    end else begin
      if (_T_125) begin
        if (_T_134) begin
          resHi <= isHi;
        end else begin
          if (_T_72) begin
            if (_T_124) begin
              resHi <= isHi;
            end else begin
              if (_T_71) begin
                resHi <= 1'h0;
              end
            end
          end else begin
            if (_T_71) begin
              resHi <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_72) begin
          if (_T_124) begin
            resHi <= isHi;
          end else begin
            if (_T_71) begin
              resHi <= 1'h0;
            end
          end
        end else begin
          if (_T_71) begin
            resHi <= 1'h0;
          end
        end
      end
    end
    if (_T_474) begin
      divisor <= _T_484;
    end else begin
      if (_T_68) begin
        if (_T_70) begin
          divisor <= subtractor;
        end
      end
    end
    if (_T_474) begin
      remainder <= {{66'd0}, lhs_in};
    end else begin
      if (_T_125) begin
        remainder <= {{1'd0}, _GEN_16};
      end else begin
        if (_T_72) begin
          remainder <= _T_120;
        end else begin
          if (_T_71) begin
            remainder <= {{66'd0}, negated_remainder};
          end else begin
            if (_T_68) begin
              if (_T_69) begin
                remainder <= {{66'd0}, negated_remainder};
              end
            end
          end
        end
      end
    end
  end
endmodule

module ALUExeUnit(
  input         clock,
  input         reset,
  output [9:0]  io_fu_types,
  input         io_req_valid,
  input         io_req_bits_uop_valid,
  input  [1:0]  io_req_bits_uop_iw_state,
  input  [8:0]  io_req_bits_uop_uopc,
  input  [31:0] io_req_bits_uop_inst,
  input  [39:0] io_req_bits_uop_pc,
  input  [1:0]  io_req_bits_uop_iqtype,
  input  [9:0]  io_req_bits_uop_fu_code,
  input  [3:0]  io_req_bits_uop_ctrl_br_type,
  input  [1:0]  io_req_bits_uop_ctrl_op1_sel,
  input  [2:0]  io_req_bits_uop_ctrl_op2_sel,
  input  [2:0]  io_req_bits_uop_ctrl_imm_sel,
  input  [3:0]  io_req_bits_uop_ctrl_op_fcn,
  input         io_req_bits_uop_ctrl_fcn_dw,
  input         io_req_bits_uop_ctrl_rf_wen,
  input  [2:0]  io_req_bits_uop_ctrl_csr_cmd,
  input         io_req_bits_uop_ctrl_is_load,
  input         io_req_bits_uop_ctrl_is_sta,
  input         io_req_bits_uop_ctrl_is_std,
  input         io_req_bits_uop_allocate_brtag,
  input         io_req_bits_uop_is_br_or_jmp,
  input         io_req_bits_uop_is_jump,
  input         io_req_bits_uop_is_jal,
  input         io_req_bits_uop_is_ret,
  input         io_req_bits_uop_is_call,
  input  [7:0]  io_req_bits_uop_br_mask,
  input  [2:0]  io_req_bits_uop_br_tag,
  input         io_req_bits_uop_br_prediction_btb_blame,
  input         io_req_bits_uop_br_prediction_btb_hit,
  input         io_req_bits_uop_br_prediction_btb_taken,
  input         io_req_bits_uop_br_prediction_bpd_blame,
  input         io_req_bits_uop_br_prediction_bpd_hit,
  input         io_req_bits_uop_br_prediction_bpd_taken,
  input  [1:0]  io_req_bits_uop_br_prediction_bim_resp_value,
  input  [5:0]  io_req_bits_uop_br_prediction_bim_resp_entry_idx,
  input  [1:0]  io_req_bits_uop_br_prediction_bim_resp_way_idx,
  input  [1:0]  io_req_bits_uop_br_prediction_bpd_resp_takens,
  input  [14:0] io_req_bits_uop_br_prediction_bpd_resp_history,
  input  [14:0] io_req_bits_uop_br_prediction_bpd_resp_history_u,
  input  [7:0]  io_req_bits_uop_br_prediction_bpd_resp_history_ptr,
  input  [14:0] io_req_bits_uop_br_prediction_bpd_resp_info,
  input         io_req_bits_uop_stat_brjmp_mispredicted,
  input         io_req_bits_uop_stat_btb_made_pred,
  input         io_req_bits_uop_stat_btb_mispredicted,
  input         io_req_bits_uop_stat_bpd_made_pred,
  input         io_req_bits_uop_stat_bpd_mispredicted,
  input  [2:0]  io_req_bits_uop_fetch_pc_lob,
  input  [19:0] io_req_bits_uop_imm_packed,
  input  [11:0] io_req_bits_uop_csr_addr,
  input  [6:0]  io_req_bits_uop_rob_idx,
  input  [3:0]  io_req_bits_uop_ldq_idx,
  input  [3:0]  io_req_bits_uop_stq_idx,
  input  [5:0]  io_req_bits_uop_brob_idx,
  input  [6:0]  io_req_bits_uop_pdst,
  input  [6:0]  io_req_bits_uop_pop1,
  input  [6:0]  io_req_bits_uop_pop2,
  input  [6:0]  io_req_bits_uop_pop3,
  input         io_req_bits_uop_prs1_busy,
  input         io_req_bits_uop_prs2_busy,
  input         io_req_bits_uop_prs3_busy,
  input  [6:0]  io_req_bits_uop_stale_pdst,
  input         io_req_bits_uop_exception,
  input  [63:0] io_req_bits_uop_exc_cause,
  input         io_req_bits_uop_bypassable,
  input  [4:0]  io_req_bits_uop_mem_cmd,
  input  [2:0]  io_req_bits_uop_mem_typ,
  input         io_req_bits_uop_is_fence,
  input         io_req_bits_uop_is_fencei,
  input         io_req_bits_uop_is_store,
  input         io_req_bits_uop_is_amo,
  input         io_req_bits_uop_is_load,
  input         io_req_bits_uop_is_sys_pc2epc,
  input         io_req_bits_uop_is_unique,
  input         io_req_bits_uop_flush_on_commit,
  input  [5:0]  io_req_bits_uop_ldst,
  input  [5:0]  io_req_bits_uop_lrs1,
  input  [5:0]  io_req_bits_uop_lrs2,
  input  [5:0]  io_req_bits_uop_lrs3,
  input         io_req_bits_uop_ldst_val,
  input  [1:0]  io_req_bits_uop_dst_rtype,
  input  [1:0]  io_req_bits_uop_lrs1_rtype,
  input  [1:0]  io_req_bits_uop_lrs2_rtype,
  input         io_req_bits_uop_frs3_en,
  input         io_req_bits_uop_fp_val,
  input         io_req_bits_uop_fp_single,
  input         io_req_bits_uop_xcpt_pf_if,
  input         io_req_bits_uop_xcpt_ae_if,
  input         io_req_bits_uop_replay_if,
  input         io_req_bits_uop_xcpt_ma_if,
  input  [63:0] io_req_bits_uop_debug_wdata,
  input  [31:0] io_req_bits_uop_debug_events_fetch_seq,
  input  [63:0] io_req_bits_rs1_data,
  input  [63:0] io_req_bits_rs2_data,
  input         io_req_bits_kill,
  output        io_resp_0_valid,
  output        io_resp_0_bits_uop_ctrl_rf_wen,
  output [2:0]  io_resp_0_bits_uop_ctrl_csr_cmd,
  output [11:0] io_resp_0_bits_uop_csr_addr,
  output [6:0]  io_resp_0_bits_uop_rob_idx,
  output [6:0]  io_resp_0_bits_uop_pdst,
  output        io_resp_0_bits_uop_bypassable,
  output        io_resp_0_bits_uop_is_store,
  output        io_resp_0_bits_uop_is_amo,
  output [1:0]  io_resp_0_bits_uop_dst_rtype,
  output [63:0] io_resp_0_bits_data,
  output        io_bypass_valid_0,
  output        io_bypass_valid_1,
  output        io_bypass_valid_2,
  output        io_bypass_uop_0_ctrl_rf_wen,
  output [6:0]  io_bypass_uop_0_pdst,
  output [1:0]  io_bypass_uop_0_dst_rtype,
  output        io_bypass_uop_1_ctrl_rf_wen,
  output [6:0]  io_bypass_uop_1_pdst,
  output [1:0]  io_bypass_uop_1_dst_rtype,
  output        io_bypass_uop_2_ctrl_rf_wen,
  output [6:0]  io_bypass_uop_2_pdst,
  output [1:0]  io_bypass_uop_2_dst_rtype,
  output [63:0] io_bypass_data_0,
  output [63:0] io_bypass_data_1,
  output [63:0] io_bypass_data_2,
  input         io_brinfo_valid,
  input         io_brinfo_mispredict,
  input  [7:0]  io_brinfo_mask,
  output        io_br_unit_take_pc,
  output [39:0] io_br_unit_target,
  output        io_br_unit_brinfo_valid,
  output        io_br_unit_brinfo_mispredict,
  output [7:0]  io_br_unit_brinfo_mask,
  output [2:0]  io_br_unit_brinfo_tag,
  output [7:0]  io_br_unit_brinfo_exe_mask,
  output [6:0]  io_br_unit_brinfo_rob_idx,
  output [3:0]  io_br_unit_brinfo_ldq_idx,
  output [3:0]  io_br_unit_brinfo_stq_idx,
  output        io_br_unit_brinfo_is_jr,
  output        io_br_unit_btb_update_valid,
  output [38:0] io_br_unit_btb_update_bits_pc,
  output [38:0] io_br_unit_btb_update_bits_target,
  output [38:0] io_br_unit_btb_update_bits_cfi_pc,
  output [2:0]  io_br_unit_btb_update_bits_bpd_type,
  output [2:0]  io_br_unit_btb_update_bits_cfi_type,
  output        io_br_unit_bim_update_valid,
  output        io_br_unit_bim_update_bits_taken,
  output [1:0]  io_br_unit_bim_update_bits_bim_resp_value,
  output [5:0]  io_br_unit_bim_update_bits_bim_resp_entry_idx,
  output [1:0]  io_br_unit_bim_update_bits_bim_resp_way_idx,
  output        io_br_unit_bpd_update_valid,
  output [2:0]  io_br_unit_bpd_update_bits_br_pc,
  output [5:0]  io_br_unit_bpd_update_bits_brob_idx,
  output        io_br_unit_bpd_update_bits_mispredict,
  output [14:0] io_br_unit_bpd_update_bits_history,
  output [7:0]  io_br_unit_bpd_update_bits_history_ptr,
  output        io_br_unit_bpd_update_bits_bpd_predict_val,
  output        io_br_unit_bpd_update_bits_bpd_mispredict,
  output        io_br_unit_bpd_update_bits_taken,
  output        io_br_unit_bpd_update_bits_is_br,
  output        io_br_unit_bpd_update_bits_new_pc_same_packet,
  output        io_br_unit_xcpt_valid,
  output [7:0]  io_br_unit_xcpt_bits_uop_br_mask,
  output [6:0]  io_br_unit_xcpt_bits_uop_rob_idx,
  output [39:0] io_br_unit_xcpt_bits_badvaddr,
  input  [39:0] io_get_rob_pc_curr_pc,
  input  [5:0]  io_get_rob_pc_curr_brob_idx,
  input         io_get_rob_pc_next_val,
  input  [39:0] io_get_rob_pc_next_pc,
  input  [1:0]  io_get_pred_info_bim_resp_value,
  input  [5:0]  io_get_pred_info_bim_resp_entry_idx,
  input  [1:0]  io_get_pred_info_bim_resp_way_idx,
  input  [14:0] io_get_pred_info_bpd_resp_history,
  input  [7:0]  io_get_pred_info_bpd_resp_history_ptr,
  input         io_status_debug
);
  wire  muldiv_busy;
  wire  _T_1210;
  wire [9:0] _T_1221;
  wire [9:0] _T_1222;
  wire [9:0] _T_1226;
  wire [9:0] _T_1230;
  wire  alu_clock;
  wire  alu_reset;
  wire  alu_io_req_valid;
  wire  alu_io_req_bits_uop_valid;
  wire [1:0] alu_io_req_bits_uop_iw_state;
  wire [8:0] alu_io_req_bits_uop_uopc;
  wire [31:0] alu_io_req_bits_uop_inst;
  wire [39:0] alu_io_req_bits_uop_pc;
  wire [1:0] alu_io_req_bits_uop_iqtype;
  wire [9:0] alu_io_req_bits_uop_fu_code;
  wire [3:0] alu_io_req_bits_uop_ctrl_br_type;
  wire [1:0] alu_io_req_bits_uop_ctrl_op1_sel;
  wire [2:0] alu_io_req_bits_uop_ctrl_op2_sel;
  wire [2:0] alu_io_req_bits_uop_ctrl_imm_sel;
  wire [3:0] alu_io_req_bits_uop_ctrl_op_fcn;
  wire  alu_io_req_bits_uop_ctrl_fcn_dw;
  wire  alu_io_req_bits_uop_ctrl_rf_wen;
  wire [2:0] alu_io_req_bits_uop_ctrl_csr_cmd;
  wire  alu_io_req_bits_uop_ctrl_is_load;
  wire  alu_io_req_bits_uop_ctrl_is_sta;
  wire  alu_io_req_bits_uop_ctrl_is_std;
  wire  alu_io_req_bits_uop_allocate_brtag;
  wire  alu_io_req_bits_uop_is_br_or_jmp;
  wire  alu_io_req_bits_uop_is_jump;
  wire  alu_io_req_bits_uop_is_jal;
  wire  alu_io_req_bits_uop_is_ret;
  wire  alu_io_req_bits_uop_is_call;
  wire [7:0] alu_io_req_bits_uop_br_mask;
  wire [2:0] alu_io_req_bits_uop_br_tag;
  wire  alu_io_req_bits_uop_br_prediction_btb_blame;
  wire  alu_io_req_bits_uop_br_prediction_btb_hit;
  wire  alu_io_req_bits_uop_br_prediction_btb_taken;
  wire  alu_io_req_bits_uop_br_prediction_bpd_blame;
  wire  alu_io_req_bits_uop_br_prediction_bpd_hit;
  wire  alu_io_req_bits_uop_br_prediction_bpd_taken;
  wire [1:0] alu_io_req_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] alu_io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] alu_io_req_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] alu_io_req_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] alu_io_req_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] alu_io_req_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] alu_io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] alu_io_req_bits_uop_br_prediction_bpd_resp_info;
  wire  alu_io_req_bits_uop_stat_brjmp_mispredicted;
  wire  alu_io_req_bits_uop_stat_btb_made_pred;
  wire  alu_io_req_bits_uop_stat_btb_mispredicted;
  wire  alu_io_req_bits_uop_stat_bpd_made_pred;
  wire  alu_io_req_bits_uop_stat_bpd_mispredicted;
  wire [2:0] alu_io_req_bits_uop_fetch_pc_lob;
  wire [19:0] alu_io_req_bits_uop_imm_packed;
  wire [11:0] alu_io_req_bits_uop_csr_addr;
  wire [6:0] alu_io_req_bits_uop_rob_idx;
  wire [3:0] alu_io_req_bits_uop_ldq_idx;
  wire [3:0] alu_io_req_bits_uop_stq_idx;
  wire [5:0] alu_io_req_bits_uop_brob_idx;
  wire [6:0] alu_io_req_bits_uop_pdst;
  wire [6:0] alu_io_req_bits_uop_pop1;
  wire [6:0] alu_io_req_bits_uop_pop2;
  wire [6:0] alu_io_req_bits_uop_pop3;
  wire  alu_io_req_bits_uop_prs1_busy;
  wire  alu_io_req_bits_uop_prs2_busy;
  wire  alu_io_req_bits_uop_prs3_busy;
  wire [6:0] alu_io_req_bits_uop_stale_pdst;
  wire  alu_io_req_bits_uop_exception;
  wire [63:0] alu_io_req_bits_uop_exc_cause;
  wire  alu_io_req_bits_uop_bypassable;
  wire [4:0] alu_io_req_bits_uop_mem_cmd;
  wire [2:0] alu_io_req_bits_uop_mem_typ;
  wire  alu_io_req_bits_uop_is_fence;
  wire  alu_io_req_bits_uop_is_fencei;
  wire  alu_io_req_bits_uop_is_store;
  wire  alu_io_req_bits_uop_is_amo;
  wire  alu_io_req_bits_uop_is_load;
  wire  alu_io_req_bits_uop_is_sys_pc2epc;
  wire  alu_io_req_bits_uop_is_unique;
  wire  alu_io_req_bits_uop_flush_on_commit;
  wire [5:0] alu_io_req_bits_uop_ldst;
  wire [5:0] alu_io_req_bits_uop_lrs1;
  wire [5:0] alu_io_req_bits_uop_lrs2;
  wire [5:0] alu_io_req_bits_uop_lrs3;
  wire  alu_io_req_bits_uop_ldst_val;
  wire [1:0] alu_io_req_bits_uop_dst_rtype;
  wire [1:0] alu_io_req_bits_uop_lrs1_rtype;
  wire [1:0] alu_io_req_bits_uop_lrs2_rtype;
  wire  alu_io_req_bits_uop_frs3_en;
  wire  alu_io_req_bits_uop_fp_val;
  wire  alu_io_req_bits_uop_fp_single;
  wire  alu_io_req_bits_uop_xcpt_pf_if;
  wire  alu_io_req_bits_uop_xcpt_ae_if;
  wire  alu_io_req_bits_uop_replay_if;
  wire  alu_io_req_bits_uop_xcpt_ma_if;
  wire [63:0] alu_io_req_bits_uop_debug_wdata;
  wire [31:0] alu_io_req_bits_uop_debug_events_fetch_seq;
  wire [63:0] alu_io_req_bits_rs1_data;
  wire [63:0] alu_io_req_bits_rs2_data;
  wire  alu_io_req_bits_kill;
  wire  alu_io_resp_valid;
  wire  alu_io_resp_bits_uop_valid;
  wire [1:0] alu_io_resp_bits_uop_iw_state;
  wire [8:0] alu_io_resp_bits_uop_uopc;
  wire [31:0] alu_io_resp_bits_uop_inst;
  wire [39:0] alu_io_resp_bits_uop_pc;
  wire [1:0] alu_io_resp_bits_uop_iqtype;
  wire [9:0] alu_io_resp_bits_uop_fu_code;
  wire [3:0] alu_io_resp_bits_uop_ctrl_br_type;
  wire [1:0] alu_io_resp_bits_uop_ctrl_op1_sel;
  wire [2:0] alu_io_resp_bits_uop_ctrl_op2_sel;
  wire [2:0] alu_io_resp_bits_uop_ctrl_imm_sel;
  wire [3:0] alu_io_resp_bits_uop_ctrl_op_fcn;
  wire  alu_io_resp_bits_uop_ctrl_fcn_dw;
  wire  alu_io_resp_bits_uop_ctrl_rf_wen;
  wire [2:0] alu_io_resp_bits_uop_ctrl_csr_cmd;
  wire  alu_io_resp_bits_uop_ctrl_is_load;
  wire  alu_io_resp_bits_uop_ctrl_is_sta;
  wire  alu_io_resp_bits_uop_ctrl_is_std;
  wire  alu_io_resp_bits_uop_allocate_brtag;
  wire  alu_io_resp_bits_uop_is_br_or_jmp;
  wire  alu_io_resp_bits_uop_is_jump;
  wire  alu_io_resp_bits_uop_is_jal;
  wire  alu_io_resp_bits_uop_is_ret;
  wire  alu_io_resp_bits_uop_is_call;
  wire [7:0] alu_io_resp_bits_uop_br_mask;
  wire [2:0] alu_io_resp_bits_uop_br_tag;
  wire  alu_io_resp_bits_uop_br_prediction_btb_blame;
  wire  alu_io_resp_bits_uop_br_prediction_btb_hit;
  wire  alu_io_resp_bits_uop_br_prediction_btb_taken;
  wire  alu_io_resp_bits_uop_br_prediction_bpd_blame;
  wire  alu_io_resp_bits_uop_br_prediction_bpd_hit;
  wire  alu_io_resp_bits_uop_br_prediction_bpd_taken;
  wire [1:0] alu_io_resp_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] alu_io_resp_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] alu_io_resp_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_info;
  wire  alu_io_resp_bits_uop_stat_brjmp_mispredicted;
  wire  alu_io_resp_bits_uop_stat_btb_made_pred;
  wire  alu_io_resp_bits_uop_stat_btb_mispredicted;
  wire  alu_io_resp_bits_uop_stat_bpd_made_pred;
  wire  alu_io_resp_bits_uop_stat_bpd_mispredicted;
  wire [2:0] alu_io_resp_bits_uop_fetch_pc_lob;
  wire [19:0] alu_io_resp_bits_uop_imm_packed;
  wire [11:0] alu_io_resp_bits_uop_csr_addr;
  wire [6:0] alu_io_resp_bits_uop_rob_idx;
  wire [3:0] alu_io_resp_bits_uop_ldq_idx;
  wire [3:0] alu_io_resp_bits_uop_stq_idx;
  wire [5:0] alu_io_resp_bits_uop_brob_idx;
  wire [6:0] alu_io_resp_bits_uop_pdst;
  wire [6:0] alu_io_resp_bits_uop_pop1;
  wire [6:0] alu_io_resp_bits_uop_pop2;
  wire [6:0] alu_io_resp_bits_uop_pop3;
  wire  alu_io_resp_bits_uop_prs1_busy;
  wire  alu_io_resp_bits_uop_prs2_busy;
  wire  alu_io_resp_bits_uop_prs3_busy;
  wire [6:0] alu_io_resp_bits_uop_stale_pdst;
  wire  alu_io_resp_bits_uop_exception;
  wire [63:0] alu_io_resp_bits_uop_exc_cause;
  wire  alu_io_resp_bits_uop_bypassable;
  wire [4:0] alu_io_resp_bits_uop_mem_cmd;
  wire [2:0] alu_io_resp_bits_uop_mem_typ;
  wire  alu_io_resp_bits_uop_is_fence;
  wire  alu_io_resp_bits_uop_is_fencei;
  wire  alu_io_resp_bits_uop_is_store;
  wire  alu_io_resp_bits_uop_is_amo;
  wire  alu_io_resp_bits_uop_is_load;
  wire  alu_io_resp_bits_uop_is_sys_pc2epc;
  wire  alu_io_resp_bits_uop_is_unique;
  wire  alu_io_resp_bits_uop_flush_on_commit;
  wire [5:0] alu_io_resp_bits_uop_ldst;
  wire [5:0] alu_io_resp_bits_uop_lrs1;
  wire [5:0] alu_io_resp_bits_uop_lrs2;
  wire [5:0] alu_io_resp_bits_uop_lrs3;
  wire  alu_io_resp_bits_uop_ldst_val;
  wire [1:0] alu_io_resp_bits_uop_dst_rtype;
  wire [1:0] alu_io_resp_bits_uop_lrs1_rtype;
  wire [1:0] alu_io_resp_bits_uop_lrs2_rtype;
  wire  alu_io_resp_bits_uop_frs3_en;
  wire  alu_io_resp_bits_uop_fp_val;
  wire  alu_io_resp_bits_uop_fp_single;
  wire  alu_io_resp_bits_uop_xcpt_pf_if;
  wire  alu_io_resp_bits_uop_xcpt_ae_if;
  wire  alu_io_resp_bits_uop_replay_if;
  wire  alu_io_resp_bits_uop_xcpt_ma_if;
  wire [63:0] alu_io_resp_bits_uop_debug_wdata;
  wire [31:0] alu_io_resp_bits_uop_debug_events_fetch_seq;
  wire [63:0] alu_io_resp_bits_data;
  wire  alu_io_brinfo_valid;
  wire  alu_io_brinfo_mispredict;
  wire [7:0] alu_io_brinfo_mask;
  wire  alu_io_bypass_valid_0;
  wire  alu_io_bypass_valid_1;
  wire  alu_io_bypass_valid_2;
  wire  alu_io_bypass_uop_0_ctrl_rf_wen;
  wire [6:0] alu_io_bypass_uop_0_pdst;
  wire [1:0] alu_io_bypass_uop_0_dst_rtype;
  wire  alu_io_bypass_uop_1_ctrl_rf_wen;
  wire [6:0] alu_io_bypass_uop_1_pdst;
  wire [1:0] alu_io_bypass_uop_1_dst_rtype;
  wire  alu_io_bypass_uop_2_ctrl_rf_wen;
  wire [6:0] alu_io_bypass_uop_2_pdst;
  wire [1:0] alu_io_bypass_uop_2_dst_rtype;
  wire [63:0] alu_io_bypass_data_0;
  wire [63:0] alu_io_bypass_data_1;
  wire [63:0] alu_io_bypass_data_2;
  wire  alu_io_br_unit_take_pc;
  wire [39:0] alu_io_br_unit_target;
  wire  alu_io_br_unit_brinfo_valid;
  wire  alu_io_br_unit_brinfo_mispredict;
  wire [7:0] alu_io_br_unit_brinfo_mask;
  wire [2:0] alu_io_br_unit_brinfo_tag;
  wire [7:0] alu_io_br_unit_brinfo_exe_mask;
  wire [6:0] alu_io_br_unit_brinfo_rob_idx;
  wire [3:0] alu_io_br_unit_brinfo_ldq_idx;
  wire [3:0] alu_io_br_unit_brinfo_stq_idx;
  wire  alu_io_br_unit_brinfo_is_jr;
  wire  alu_io_br_unit_btb_update_valid;
  wire [38:0] alu_io_br_unit_btb_update_bits_pc;
  wire [38:0] alu_io_br_unit_btb_update_bits_target;
  wire [38:0] alu_io_br_unit_btb_update_bits_cfi_pc;
  wire [2:0] alu_io_br_unit_btb_update_bits_bpd_type;
  wire [2:0] alu_io_br_unit_btb_update_bits_cfi_type;
  wire  alu_io_br_unit_bim_update_valid;
  wire  alu_io_br_unit_bim_update_bits_taken;
  wire [1:0] alu_io_br_unit_bim_update_bits_bim_resp_value;
  wire [5:0] alu_io_br_unit_bim_update_bits_bim_resp_entry_idx;
  wire [1:0] alu_io_br_unit_bim_update_bits_bim_resp_way_idx;
  wire  alu_io_br_unit_bpd_update_valid;
  wire [2:0] alu_io_br_unit_bpd_update_bits_br_pc;
  wire [5:0] alu_io_br_unit_bpd_update_bits_brob_idx;
  wire  alu_io_br_unit_bpd_update_bits_mispredict;
  wire [14:0] alu_io_br_unit_bpd_update_bits_history;
  wire [7:0] alu_io_br_unit_bpd_update_bits_history_ptr;
  wire  alu_io_br_unit_bpd_update_bits_bpd_predict_val;
  wire  alu_io_br_unit_bpd_update_bits_bpd_mispredict;
  wire  alu_io_br_unit_bpd_update_bits_taken;
  wire  alu_io_br_unit_bpd_update_bits_is_br;
  wire  alu_io_br_unit_bpd_update_bits_new_pc_same_packet;
  wire  alu_io_br_unit_xcpt_valid;
  wire [7:0] alu_io_br_unit_xcpt_bits_uop_br_mask;
  wire [6:0] alu_io_br_unit_xcpt_bits_uop_rob_idx;
  wire [39:0] alu_io_br_unit_xcpt_bits_badvaddr;
  wire [39:0] alu_io_get_rob_pc_curr_pc;
  wire [5:0] alu_io_get_rob_pc_curr_brob_idx;
  wire  alu_io_get_rob_pc_next_val;
  wire [39:0] alu_io_get_rob_pc_next_pc;
  wire [1:0] alu_io_get_pred_info_bim_resp_value;
  wire [5:0] alu_io_get_pred_info_bim_resp_entry_idx;
  wire [1:0] alu_io_get_pred_info_bim_resp_way_idx;
  wire [14:0] alu_io_get_pred_info_bpd_resp_history;
  wire [7:0] alu_io_get_pred_info_bpd_resp_history_ptr;
  wire  alu_io_status_debug;
  wire  _T_1238;
  wire  _T_1239;
  wire  _T_1240;
  wire  _T_1241;
  wire  _T_1242;
  wire  _T_1243;
  wire  fu_units_1_clock;
  wire  fu_units_1_reset;
  wire  fu_units_1_io_req_valid;
  wire  fu_units_1_io_req_bits_uop_valid;
  wire [1:0] fu_units_1_io_req_bits_uop_iw_state;
  wire [8:0] fu_units_1_io_req_bits_uop_uopc;
  wire [31:0] fu_units_1_io_req_bits_uop_inst;
  wire [39:0] fu_units_1_io_req_bits_uop_pc;
  wire [1:0] fu_units_1_io_req_bits_uop_iqtype;
  wire [9:0] fu_units_1_io_req_bits_uop_fu_code;
  wire [3:0] fu_units_1_io_req_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_1_io_req_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_1_io_req_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_1_io_req_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_1_io_req_bits_uop_ctrl_op_fcn;
  wire  fu_units_1_io_req_bits_uop_ctrl_fcn_dw;
  wire  fu_units_1_io_req_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_1_io_req_bits_uop_ctrl_csr_cmd;
  wire  fu_units_1_io_req_bits_uop_ctrl_is_load;
  wire  fu_units_1_io_req_bits_uop_ctrl_is_sta;
  wire  fu_units_1_io_req_bits_uop_ctrl_is_std;
  wire  fu_units_1_io_req_bits_uop_allocate_brtag;
  wire  fu_units_1_io_req_bits_uop_is_br_or_jmp;
  wire  fu_units_1_io_req_bits_uop_is_jump;
  wire  fu_units_1_io_req_bits_uop_is_jal;
  wire  fu_units_1_io_req_bits_uop_is_ret;
  wire  fu_units_1_io_req_bits_uop_is_call;
  wire [7:0] fu_units_1_io_req_bits_uop_br_mask;
  wire [2:0] fu_units_1_io_req_bits_uop_br_tag;
  wire  fu_units_1_io_req_bits_uop_br_prediction_btb_blame;
  wire  fu_units_1_io_req_bits_uop_br_prediction_btb_hit;
  wire  fu_units_1_io_req_bits_uop_br_prediction_btb_taken;
  wire  fu_units_1_io_req_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_1_io_req_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_1_io_req_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_1_io_req_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_1_io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_1_io_req_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_1_io_req_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_1_io_req_bits_uop_stat_btb_made_pred;
  wire  fu_units_1_io_req_bits_uop_stat_btb_mispredicted;
  wire  fu_units_1_io_req_bits_uop_stat_bpd_made_pred;
  wire  fu_units_1_io_req_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_1_io_req_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_1_io_req_bits_uop_imm_packed;
  wire [11:0] fu_units_1_io_req_bits_uop_csr_addr;
  wire [6:0] fu_units_1_io_req_bits_uop_rob_idx;
  wire [3:0] fu_units_1_io_req_bits_uop_ldq_idx;
  wire [3:0] fu_units_1_io_req_bits_uop_stq_idx;
  wire [5:0] fu_units_1_io_req_bits_uop_brob_idx;
  wire [6:0] fu_units_1_io_req_bits_uop_pdst;
  wire [6:0] fu_units_1_io_req_bits_uop_pop1;
  wire [6:0] fu_units_1_io_req_bits_uop_pop2;
  wire [6:0] fu_units_1_io_req_bits_uop_pop3;
  wire  fu_units_1_io_req_bits_uop_prs1_busy;
  wire  fu_units_1_io_req_bits_uop_prs2_busy;
  wire  fu_units_1_io_req_bits_uop_prs3_busy;
  wire [6:0] fu_units_1_io_req_bits_uop_stale_pdst;
  wire  fu_units_1_io_req_bits_uop_exception;
  wire [63:0] fu_units_1_io_req_bits_uop_exc_cause;
  wire  fu_units_1_io_req_bits_uop_bypassable;
  wire [4:0] fu_units_1_io_req_bits_uop_mem_cmd;
  wire [2:0] fu_units_1_io_req_bits_uop_mem_typ;
  wire  fu_units_1_io_req_bits_uop_is_fence;
  wire  fu_units_1_io_req_bits_uop_is_fencei;
  wire  fu_units_1_io_req_bits_uop_is_store;
  wire  fu_units_1_io_req_bits_uop_is_amo;
  wire  fu_units_1_io_req_bits_uop_is_load;
  wire  fu_units_1_io_req_bits_uop_is_sys_pc2epc;
  wire  fu_units_1_io_req_bits_uop_is_unique;
  wire  fu_units_1_io_req_bits_uop_flush_on_commit;
  wire [5:0] fu_units_1_io_req_bits_uop_ldst;
  wire [5:0] fu_units_1_io_req_bits_uop_lrs1;
  wire [5:0] fu_units_1_io_req_bits_uop_lrs2;
  wire [5:0] fu_units_1_io_req_bits_uop_lrs3;
  wire  fu_units_1_io_req_bits_uop_ldst_val;
  wire [1:0] fu_units_1_io_req_bits_uop_dst_rtype;
  wire [1:0] fu_units_1_io_req_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_1_io_req_bits_uop_lrs2_rtype;
  wire  fu_units_1_io_req_bits_uop_frs3_en;
  wire  fu_units_1_io_req_bits_uop_fp_val;
  wire  fu_units_1_io_req_bits_uop_fp_single;
  wire  fu_units_1_io_req_bits_uop_xcpt_pf_if;
  wire  fu_units_1_io_req_bits_uop_xcpt_ae_if;
  wire  fu_units_1_io_req_bits_uop_replay_if;
  wire  fu_units_1_io_req_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_1_io_req_bits_uop_debug_wdata;
  wire [31:0] fu_units_1_io_req_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_1_io_req_bits_rs1_data;
  wire [63:0] fu_units_1_io_req_bits_rs2_data;
  wire  fu_units_1_io_req_bits_kill;
  wire  fu_units_1_io_resp_valid;
  wire  fu_units_1_io_resp_bits_uop_valid;
  wire [1:0] fu_units_1_io_resp_bits_uop_iw_state;
  wire [8:0] fu_units_1_io_resp_bits_uop_uopc;
  wire [31:0] fu_units_1_io_resp_bits_uop_inst;
  wire [39:0] fu_units_1_io_resp_bits_uop_pc;
  wire [1:0] fu_units_1_io_resp_bits_uop_iqtype;
  wire [9:0] fu_units_1_io_resp_bits_uop_fu_code;
  wire [3:0] fu_units_1_io_resp_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_1_io_resp_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_1_io_resp_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_1_io_resp_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_1_io_resp_bits_uop_ctrl_op_fcn;
  wire  fu_units_1_io_resp_bits_uop_ctrl_fcn_dw;
  wire  fu_units_1_io_resp_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_1_io_resp_bits_uop_ctrl_csr_cmd;
  wire  fu_units_1_io_resp_bits_uop_ctrl_is_load;
  wire  fu_units_1_io_resp_bits_uop_ctrl_is_sta;
  wire  fu_units_1_io_resp_bits_uop_ctrl_is_std;
  wire  fu_units_1_io_resp_bits_uop_allocate_brtag;
  wire  fu_units_1_io_resp_bits_uop_is_br_or_jmp;
  wire  fu_units_1_io_resp_bits_uop_is_jump;
  wire  fu_units_1_io_resp_bits_uop_is_jal;
  wire  fu_units_1_io_resp_bits_uop_is_ret;
  wire  fu_units_1_io_resp_bits_uop_is_call;
  wire [7:0] fu_units_1_io_resp_bits_uop_br_mask;
  wire [2:0] fu_units_1_io_resp_bits_uop_br_tag;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_btb_blame;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_btb_hit;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_btb_taken;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_1_io_resp_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_1_io_resp_bits_uop_stat_btb_made_pred;
  wire  fu_units_1_io_resp_bits_uop_stat_btb_mispredicted;
  wire  fu_units_1_io_resp_bits_uop_stat_bpd_made_pred;
  wire  fu_units_1_io_resp_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_1_io_resp_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_1_io_resp_bits_uop_imm_packed;
  wire [11:0] fu_units_1_io_resp_bits_uop_csr_addr;
  wire [6:0] fu_units_1_io_resp_bits_uop_rob_idx;
  wire [3:0] fu_units_1_io_resp_bits_uop_ldq_idx;
  wire [3:0] fu_units_1_io_resp_bits_uop_stq_idx;
  wire [5:0] fu_units_1_io_resp_bits_uop_brob_idx;
  wire [6:0] fu_units_1_io_resp_bits_uop_pdst;
  wire [6:0] fu_units_1_io_resp_bits_uop_pop1;
  wire [6:0] fu_units_1_io_resp_bits_uop_pop2;
  wire [6:0] fu_units_1_io_resp_bits_uop_pop3;
  wire  fu_units_1_io_resp_bits_uop_prs1_busy;
  wire  fu_units_1_io_resp_bits_uop_prs2_busy;
  wire  fu_units_1_io_resp_bits_uop_prs3_busy;
  wire [6:0] fu_units_1_io_resp_bits_uop_stale_pdst;
  wire  fu_units_1_io_resp_bits_uop_exception;
  wire [63:0] fu_units_1_io_resp_bits_uop_exc_cause;
  wire  fu_units_1_io_resp_bits_uop_bypassable;
  wire [4:0] fu_units_1_io_resp_bits_uop_mem_cmd;
  wire [2:0] fu_units_1_io_resp_bits_uop_mem_typ;
  wire  fu_units_1_io_resp_bits_uop_is_fence;
  wire  fu_units_1_io_resp_bits_uop_is_fencei;
  wire  fu_units_1_io_resp_bits_uop_is_store;
  wire  fu_units_1_io_resp_bits_uop_is_amo;
  wire  fu_units_1_io_resp_bits_uop_is_load;
  wire  fu_units_1_io_resp_bits_uop_is_sys_pc2epc;
  wire  fu_units_1_io_resp_bits_uop_is_unique;
  wire  fu_units_1_io_resp_bits_uop_flush_on_commit;
  wire [5:0] fu_units_1_io_resp_bits_uop_ldst;
  wire [5:0] fu_units_1_io_resp_bits_uop_lrs1;
  wire [5:0] fu_units_1_io_resp_bits_uop_lrs2;
  wire [5:0] fu_units_1_io_resp_bits_uop_lrs3;
  wire  fu_units_1_io_resp_bits_uop_ldst_val;
  wire [1:0] fu_units_1_io_resp_bits_uop_dst_rtype;
  wire [1:0] fu_units_1_io_resp_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_1_io_resp_bits_uop_lrs2_rtype;
  wire  fu_units_1_io_resp_bits_uop_frs3_en;
  wire  fu_units_1_io_resp_bits_uop_fp_val;
  wire  fu_units_1_io_resp_bits_uop_fp_single;
  wire  fu_units_1_io_resp_bits_uop_xcpt_pf_if;
  wire  fu_units_1_io_resp_bits_uop_xcpt_ae_if;
  wire  fu_units_1_io_resp_bits_uop_replay_if;
  wire  fu_units_1_io_resp_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_1_io_resp_bits_uop_debug_wdata;
  wire [31:0] fu_units_1_io_resp_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_1_io_resp_bits_data;
  wire  fu_units_1_io_brinfo_valid;
  wire  fu_units_1_io_brinfo_mispredict;
  wire [7:0] fu_units_1_io_brinfo_mask;
  wire  _T_1244;
  wire  _T_1245;
  wire  muldiv_resp_val;
  wire  fu_units_2_clock;
  wire  fu_units_2_reset;
  wire  fu_units_2_io_req_ready;
  wire  fu_units_2_io_req_valid;
  wire  fu_units_2_io_req_bits_uop_valid;
  wire [1:0] fu_units_2_io_req_bits_uop_iw_state;
  wire [8:0] fu_units_2_io_req_bits_uop_uopc;
  wire [31:0] fu_units_2_io_req_bits_uop_inst;
  wire [39:0] fu_units_2_io_req_bits_uop_pc;
  wire [1:0] fu_units_2_io_req_bits_uop_iqtype;
  wire [9:0] fu_units_2_io_req_bits_uop_fu_code;
  wire [3:0] fu_units_2_io_req_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_2_io_req_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_2_io_req_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_2_io_req_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_2_io_req_bits_uop_ctrl_op_fcn;
  wire  fu_units_2_io_req_bits_uop_ctrl_fcn_dw;
  wire  fu_units_2_io_req_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_2_io_req_bits_uop_ctrl_csr_cmd;
  wire  fu_units_2_io_req_bits_uop_ctrl_is_load;
  wire  fu_units_2_io_req_bits_uop_ctrl_is_sta;
  wire  fu_units_2_io_req_bits_uop_ctrl_is_std;
  wire  fu_units_2_io_req_bits_uop_allocate_brtag;
  wire  fu_units_2_io_req_bits_uop_is_br_or_jmp;
  wire  fu_units_2_io_req_bits_uop_is_jump;
  wire  fu_units_2_io_req_bits_uop_is_jal;
  wire  fu_units_2_io_req_bits_uop_is_ret;
  wire  fu_units_2_io_req_bits_uop_is_call;
  wire [7:0] fu_units_2_io_req_bits_uop_br_mask;
  wire [2:0] fu_units_2_io_req_bits_uop_br_tag;
  wire  fu_units_2_io_req_bits_uop_br_prediction_btb_blame;
  wire  fu_units_2_io_req_bits_uop_br_prediction_btb_hit;
  wire  fu_units_2_io_req_bits_uop_br_prediction_btb_taken;
  wire  fu_units_2_io_req_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_2_io_req_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_2_io_req_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_2_io_req_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_2_io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_2_io_req_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_2_io_req_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_2_io_req_bits_uop_stat_btb_made_pred;
  wire  fu_units_2_io_req_bits_uop_stat_btb_mispredicted;
  wire  fu_units_2_io_req_bits_uop_stat_bpd_made_pred;
  wire  fu_units_2_io_req_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_2_io_req_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_2_io_req_bits_uop_imm_packed;
  wire [11:0] fu_units_2_io_req_bits_uop_csr_addr;
  wire [6:0] fu_units_2_io_req_bits_uop_rob_idx;
  wire [3:0] fu_units_2_io_req_bits_uop_ldq_idx;
  wire [3:0] fu_units_2_io_req_bits_uop_stq_idx;
  wire [5:0] fu_units_2_io_req_bits_uop_brob_idx;
  wire [6:0] fu_units_2_io_req_bits_uop_pdst;
  wire [6:0] fu_units_2_io_req_bits_uop_pop1;
  wire [6:0] fu_units_2_io_req_bits_uop_pop2;
  wire [6:0] fu_units_2_io_req_bits_uop_pop3;
  wire  fu_units_2_io_req_bits_uop_prs1_busy;
  wire  fu_units_2_io_req_bits_uop_prs2_busy;
  wire  fu_units_2_io_req_bits_uop_prs3_busy;
  wire [6:0] fu_units_2_io_req_bits_uop_stale_pdst;
  wire  fu_units_2_io_req_bits_uop_exception;
  wire [63:0] fu_units_2_io_req_bits_uop_exc_cause;
  wire  fu_units_2_io_req_bits_uop_bypassable;
  wire [4:0] fu_units_2_io_req_bits_uop_mem_cmd;
  wire [2:0] fu_units_2_io_req_bits_uop_mem_typ;
  wire  fu_units_2_io_req_bits_uop_is_fence;
  wire  fu_units_2_io_req_bits_uop_is_fencei;
  wire  fu_units_2_io_req_bits_uop_is_store;
  wire  fu_units_2_io_req_bits_uop_is_amo;
  wire  fu_units_2_io_req_bits_uop_is_load;
  wire  fu_units_2_io_req_bits_uop_is_sys_pc2epc;
  wire  fu_units_2_io_req_bits_uop_is_unique;
  wire  fu_units_2_io_req_bits_uop_flush_on_commit;
  wire [5:0] fu_units_2_io_req_bits_uop_ldst;
  wire [5:0] fu_units_2_io_req_bits_uop_lrs1;
  wire [5:0] fu_units_2_io_req_bits_uop_lrs2;
  wire [5:0] fu_units_2_io_req_bits_uop_lrs3;
  wire  fu_units_2_io_req_bits_uop_ldst_val;
  wire [1:0] fu_units_2_io_req_bits_uop_dst_rtype;
  wire [1:0] fu_units_2_io_req_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_2_io_req_bits_uop_lrs2_rtype;
  wire  fu_units_2_io_req_bits_uop_frs3_en;
  wire  fu_units_2_io_req_bits_uop_fp_val;
  wire  fu_units_2_io_req_bits_uop_fp_single;
  wire  fu_units_2_io_req_bits_uop_xcpt_pf_if;
  wire  fu_units_2_io_req_bits_uop_xcpt_ae_if;
  wire  fu_units_2_io_req_bits_uop_replay_if;
  wire  fu_units_2_io_req_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_2_io_req_bits_uop_debug_wdata;
  wire [31:0] fu_units_2_io_req_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_2_io_req_bits_rs1_data;
  wire [63:0] fu_units_2_io_req_bits_rs2_data;
  wire  fu_units_2_io_req_bits_kill;
  wire  fu_units_2_io_resp_ready;
  wire  fu_units_2_io_resp_valid;
  wire  fu_units_2_io_resp_bits_uop_valid;
  wire [1:0] fu_units_2_io_resp_bits_uop_iw_state;
  wire [8:0] fu_units_2_io_resp_bits_uop_uopc;
  wire [31:0] fu_units_2_io_resp_bits_uop_inst;
  wire [39:0] fu_units_2_io_resp_bits_uop_pc;
  wire [1:0] fu_units_2_io_resp_bits_uop_iqtype;
  wire [9:0] fu_units_2_io_resp_bits_uop_fu_code;
  wire [3:0] fu_units_2_io_resp_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_2_io_resp_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_2_io_resp_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_2_io_resp_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_2_io_resp_bits_uop_ctrl_op_fcn;
  wire  fu_units_2_io_resp_bits_uop_ctrl_fcn_dw;
  wire  fu_units_2_io_resp_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_2_io_resp_bits_uop_ctrl_csr_cmd;
  wire  fu_units_2_io_resp_bits_uop_ctrl_is_load;
  wire  fu_units_2_io_resp_bits_uop_ctrl_is_sta;
  wire  fu_units_2_io_resp_bits_uop_ctrl_is_std;
  wire  fu_units_2_io_resp_bits_uop_allocate_brtag;
  wire  fu_units_2_io_resp_bits_uop_is_br_or_jmp;
  wire  fu_units_2_io_resp_bits_uop_is_jump;
  wire  fu_units_2_io_resp_bits_uop_is_jal;
  wire  fu_units_2_io_resp_bits_uop_is_ret;
  wire  fu_units_2_io_resp_bits_uop_is_call;
  wire [7:0] fu_units_2_io_resp_bits_uop_br_mask;
  wire [2:0] fu_units_2_io_resp_bits_uop_br_tag;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_btb_blame;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_btb_hit;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_btb_taken;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_2_io_resp_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_2_io_resp_bits_uop_stat_btb_made_pred;
  wire  fu_units_2_io_resp_bits_uop_stat_btb_mispredicted;
  wire  fu_units_2_io_resp_bits_uop_stat_bpd_made_pred;
  wire  fu_units_2_io_resp_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_2_io_resp_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_2_io_resp_bits_uop_imm_packed;
  wire [11:0] fu_units_2_io_resp_bits_uop_csr_addr;
  wire [6:0] fu_units_2_io_resp_bits_uop_rob_idx;
  wire [3:0] fu_units_2_io_resp_bits_uop_ldq_idx;
  wire [3:0] fu_units_2_io_resp_bits_uop_stq_idx;
  wire [5:0] fu_units_2_io_resp_bits_uop_brob_idx;
  wire [6:0] fu_units_2_io_resp_bits_uop_pdst;
  wire [6:0] fu_units_2_io_resp_bits_uop_pop1;
  wire [6:0] fu_units_2_io_resp_bits_uop_pop2;
  wire [6:0] fu_units_2_io_resp_bits_uop_pop3;
  wire  fu_units_2_io_resp_bits_uop_prs1_busy;
  wire  fu_units_2_io_resp_bits_uop_prs2_busy;
  wire  fu_units_2_io_resp_bits_uop_prs3_busy;
  wire [6:0] fu_units_2_io_resp_bits_uop_stale_pdst;
  wire  fu_units_2_io_resp_bits_uop_exception;
  wire [63:0] fu_units_2_io_resp_bits_uop_exc_cause;
  wire  fu_units_2_io_resp_bits_uop_bypassable;
  wire [4:0] fu_units_2_io_resp_bits_uop_mem_cmd;
  wire [2:0] fu_units_2_io_resp_bits_uop_mem_typ;
  wire  fu_units_2_io_resp_bits_uop_is_fence;
  wire  fu_units_2_io_resp_bits_uop_is_fencei;
  wire  fu_units_2_io_resp_bits_uop_is_store;
  wire  fu_units_2_io_resp_bits_uop_is_amo;
  wire  fu_units_2_io_resp_bits_uop_is_load;
  wire  fu_units_2_io_resp_bits_uop_is_sys_pc2epc;
  wire  fu_units_2_io_resp_bits_uop_is_unique;
  wire  fu_units_2_io_resp_bits_uop_flush_on_commit;
  wire [5:0] fu_units_2_io_resp_bits_uop_ldst;
  wire [5:0] fu_units_2_io_resp_bits_uop_lrs1;
  wire [5:0] fu_units_2_io_resp_bits_uop_lrs2;
  wire [5:0] fu_units_2_io_resp_bits_uop_lrs3;
  wire  fu_units_2_io_resp_bits_uop_ldst_val;
  wire [1:0] fu_units_2_io_resp_bits_uop_dst_rtype;
  wire [1:0] fu_units_2_io_resp_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_2_io_resp_bits_uop_lrs2_rtype;
  wire  fu_units_2_io_resp_bits_uop_frs3_en;
  wire  fu_units_2_io_resp_bits_uop_fp_val;
  wire  fu_units_2_io_resp_bits_uop_fp_single;
  wire  fu_units_2_io_resp_bits_uop_xcpt_pf_if;
  wire  fu_units_2_io_resp_bits_uop_xcpt_ae_if;
  wire  fu_units_2_io_resp_bits_uop_replay_if;
  wire  fu_units_2_io_resp_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_2_io_resp_bits_uop_debug_wdata;
  wire [31:0] fu_units_2_io_resp_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_2_io_resp_bits_data;
  wire  fu_units_2_io_brinfo_valid;
  wire  fu_units_2_io_brinfo_mispredict;
  wire [7:0] fu_units_2_io_brinfo_mask;
  wire  _T_1326;
  wire  _T_1333;
  wire  _T_1334;
  wire  _T_1336;
  wire  _T_1338;
  wire  _T_1345;
  wire  _T_1351;
  wire [95:0] _T_1357;
  wire [1:0] _T_1358;
  wire [2:0] _T_1359;
  wire [98:0] _T_1360;
  wire [1:0] _T_1361;
  wire [2:0] _T_1362;
  wire [3:0] _T_1363;
  wire [4:0] _T_1364;
  wire [7:0] _T_1365;
  wire [106:0] _T_1366;
  wire [2:0] _T_1367;
  wire [11:0] _T_1368;
  wire [17:0] _T_1369;
  wire [20:0] _T_1370;
  wire [1:0] _T_1371;
  wire [7:0] _T_1372;
  wire [1:0] _T_1373;
  wire [2:0] _T_1374;
  wire [10:0] _T_1375;
  wire [31:0] _T_1376;
  wire [138:0] _T_1377;
  wire [1:0] _T_1378;
  wire [7:0] _T_1379;
  wire [8:0] _T_1380;
  wire [10:0] _T_1381;
  wire [64:0] _T_1382;
  wire [65:0] _T_1383;
  wire [1:0] _T_1384;
  wire [8:0] _T_1385;
  wire [74:0] _T_1386;
  wire [85:0] _T_1387;
  wire [13:0] _T_1388;
  wire [14:0] _T_1389;
  wire [12:0] _T_1390;
  wire [19:0] _T_1391;
  wire [34:0] _T_1392;
  wire [10:0] _T_1393;
  wire [14:0] _T_1394;
  wire [22:0] _T_1395;
  wire [34:0] _T_1396;
  wire [49:0] _T_1397;
  wire [84:0] _T_1398;
  wire [170:0] _T_1399;
  wire [309:0] _T_1400;
  wire [1:0] _T_1401;
  wire [1:0] _T_1402;
  wire [2:0] _T_1403;
  wire [4:0] _T_1404;
  wire [22:0] _T_1405;
  wire [37:0] _T_1406;
  wire [3:0] _T_1407;
  wire [18:0] _T_1408;
  wire [56:0] _T_1409;
  wire [61:0] _T_1410;
  wire [7:0] _T_1411;
  wire [1:0] _T_1412;
  wire [2:0] _T_1413;
  wire [10:0] _T_1414;
  wire [1:0] _T_1415;
  wire [2:0] _T_1416;
  wire [8:0] _T_1417;
  wire [11:0] _T_1418;
  wire [14:0] _T_1419;
  wire [25:0] _T_1420;
  wire [87:0] _T_1421;
  wire [1:0] _T_1422;
  wire [1:0] _T_1423;
  wire [2:0] _T_1424;
  wire [4:0] _T_1425;
  wire [1:0] _T_1426;
  wire [2:0] _T_1427;
  wire [1:0] _T_1428;
  wire [4:0] _T_1429;
  wire [7:0] _T_1430;
  wire [12:0] _T_1431;
  wire [5:0] _T_1432;
  wire [9:0] _T_1433;
  wire [13:0] _T_1434;
  wire [15:0] _T_1435;
  wire [25:0] _T_1436;
  wire [71:0] _T_1437;
  wire [73:0] _T_1438;
  wire [2:0] _T_1439;
  wire [11:0] _T_1440;
  wire [85:0] _T_1441;
  wire [111:0] _T_1442;
  wire [124:0] _T_1443;
  wire [212:0] _T_1444;
  wire [522:0] _T_1445;
  wire [95:0] _T_1446;
  wire [1:0] _T_1447;
  wire [2:0] _T_1448;
  wire [98:0] _T_1449;
  wire [1:0] _T_1450;
  wire [2:0] _T_1451;
  wire [3:0] _T_1452;
  wire [4:0] _T_1453;
  wire [7:0] _T_1454;
  wire [106:0] _T_1455;
  wire [2:0] _T_1456;
  wire [11:0] _T_1457;
  wire [17:0] _T_1458;
  wire [20:0] _T_1459;
  wire [1:0] _T_1460;
  wire [7:0] _T_1461;
  wire [1:0] _T_1462;
  wire [2:0] _T_1463;
  wire [10:0] _T_1464;
  wire [31:0] _T_1465;
  wire [138:0] _T_1466;
  wire [1:0] _T_1467;
  wire [7:0] _T_1468;
  wire [8:0] _T_1469;
  wire [10:0] _T_1470;
  wire [64:0] _T_1471;
  wire [65:0] _T_1472;
  wire [1:0] _T_1473;
  wire [8:0] _T_1474;
  wire [74:0] _T_1475;
  wire [85:0] _T_1476;
  wire [13:0] _T_1477;
  wire [14:0] _T_1478;
  wire [12:0] _T_1479;
  wire [19:0] _T_1480;
  wire [34:0] _T_1481;
  wire [10:0] _T_1482;
  wire [14:0] _T_1483;
  wire [22:0] _T_1484;
  wire [34:0] _T_1485;
  wire [49:0] _T_1486;
  wire [84:0] _T_1487;
  wire [170:0] _T_1488;
  wire [309:0] _T_1489;
  wire [1:0] _T_1490;
  wire [1:0] _T_1491;
  wire [2:0] _T_1492;
  wire [4:0] _T_1493;
  wire [22:0] _T_1494;
  wire [37:0] _T_1495;
  wire [3:0] _T_1496;
  wire [18:0] _T_1497;
  wire [56:0] _T_1498;
  wire [61:0] _T_1499;
  wire [7:0] _T_1500;
  wire [1:0] _T_1501;
  wire [2:0] _T_1502;
  wire [10:0] _T_1503;
  wire [1:0] _T_1504;
  wire [2:0] _T_1505;
  wire [8:0] _T_1506;
  wire [11:0] _T_1507;
  wire [14:0] _T_1508;
  wire [25:0] _T_1509;
  wire [87:0] _T_1510;
  wire [1:0] _T_1511;
  wire [1:0] _T_1512;
  wire [2:0] _T_1513;
  wire [4:0] _T_1514;
  wire [1:0] _T_1515;
  wire [2:0] _T_1516;
  wire [1:0] _T_1517;
  wire [4:0] _T_1518;
  wire [7:0] _T_1519;
  wire [12:0] _T_1520;
  wire [5:0] _T_1521;
  wire [9:0] _T_1522;
  wire [13:0] _T_1523;
  wire [15:0] _T_1524;
  wire [25:0] _T_1525;
  wire [71:0] _T_1526;
  wire [73:0] _T_1527;
  wire [2:0] _T_1528;
  wire [11:0] _T_1529;
  wire [85:0] _T_1530;
  wire [111:0] _T_1531;
  wire [124:0] _T_1532;
  wire [212:0] _T_1533;
  wire [522:0] _T_1534;
  wire [95:0] _T_1535;
  wire [1:0] _T_1536;
  wire [2:0] _T_1537;
  wire [98:0] _T_1538;
  wire [1:0] _T_1539;
  wire [2:0] _T_1540;
  wire [3:0] _T_1541;
  wire [4:0] _T_1542;
  wire [7:0] _T_1543;
  wire [106:0] _T_1544;
  wire [2:0] _T_1545;
  wire [11:0] _T_1546;
  wire [17:0] _T_1547;
  wire [20:0] _T_1548;
  wire [1:0] _T_1549;
  wire [7:0] _T_1550;
  wire [1:0] _T_1551;
  wire [2:0] _T_1552;
  wire [10:0] _T_1553;
  wire [31:0] _T_1554;
  wire [138:0] _T_1555;
  wire [1:0] _T_1556;
  wire [7:0] _T_1557;
  wire [8:0] _T_1558;
  wire [10:0] _T_1559;
  wire [64:0] _T_1560;
  wire [65:0] _T_1561;
  wire [1:0] _T_1562;
  wire [8:0] _T_1563;
  wire [74:0] _T_1564;
  wire [85:0] _T_1565;
  wire [13:0] _T_1566;
  wire [14:0] _T_1567;
  wire [12:0] _T_1568;
  wire [19:0] _T_1569;
  wire [34:0] _T_1570;
  wire [10:0] _T_1571;
  wire [14:0] _T_1572;
  wire [22:0] _T_1573;
  wire [34:0] _T_1574;
  wire [49:0] _T_1575;
  wire [84:0] _T_1576;
  wire [170:0] _T_1577;
  wire [309:0] _T_1578;
  wire [1:0] _T_1579;
  wire [1:0] _T_1580;
  wire [2:0] _T_1581;
  wire [4:0] _T_1582;
  wire [22:0] _T_1583;
  wire [37:0] _T_1584;
  wire [3:0] _T_1585;
  wire [18:0] _T_1586;
  wire [56:0] _T_1587;
  wire [61:0] _T_1588;
  wire [7:0] _T_1589;
  wire [1:0] _T_1590;
  wire [2:0] _T_1591;
  wire [10:0] _T_1592;
  wire [1:0] _T_1593;
  wire [2:0] _T_1594;
  wire [8:0] _T_1595;
  wire [11:0] _T_1596;
  wire [14:0] _T_1597;
  wire [25:0] _T_1598;
  wire [87:0] _T_1599;
  wire [1:0] _T_1600;
  wire [1:0] _T_1601;
  wire [2:0] _T_1602;
  wire [4:0] _T_1603;
  wire [1:0] _T_1604;
  wire [2:0] _T_1605;
  wire [1:0] _T_1606;
  wire [4:0] _T_1607;
  wire [7:0] _T_1608;
  wire [12:0] _T_1609;
  wire [5:0] _T_1610;
  wire [9:0] _T_1611;
  wire [13:0] _T_1612;
  wire [15:0] _T_1613;
  wire [25:0] _T_1614;
  wire [71:0] _T_1615;
  wire [73:0] _T_1616;
  wire [2:0] _T_1617;
  wire [11:0] _T_1618;
  wire [85:0] _T_1619;
  wire [111:0] _T_1620;
  wire [124:0] _T_1621;
  wire [212:0] _T_1622;
  wire [522:0] _T_1623;
  wire [522:0] _T_1624;
  wire [522:0] _T_1625;
  wire  _T_1631_ctrl_rf_wen;
  wire [6:0] _T_1631_rob_idx;
  wire [6:0] _T_1631_pdst;
  wire  _T_1631_bypassable;
  wire  _T_1631_is_store;
  wire  _T_1631_is_amo;
  wire [1:0] _T_1631_dst_rtype;
  wire [522:0] _T_1637;
  wire [1:0] _T_1649;
  wire  _T_1659;
  wire  _T_1660;
  wire  _T_1665;
  wire [6:0] _T_1675;
  wire [6:0] _T_1679;
  wire  _T_1714;
  wire [63:0] _T_1728;
  wire [63:0] _T_1729;
  wire  _T_1730;
  wire  _T_1731;
  wire [10:0] _T_1735;
  wire [7:0] _T_1741;
  wire  _T_1747;
  wire  _T_1748;
  wire [4:0] _T_1753;
  wire [4:0] _T_1754;
  wire [4:0] _T_1758;
  wire [4:0] _T_1759;
  wire  _T_1768;
  wire [4:0] _T_1769;
  wire [4:0] _T_1770;
  wire [9:0] _T_1771;
  wire [10:0] _T_1772;
  wire  _T_1773;
  wire [7:0] _T_1774;
  wire [8:0] _T_1775;
  wire [10:0] _T_1776;
  wire [11:0] _T_1778;
  wire [20:0] _T_1779;
  wire [31:0] _T_1780;
  wire [31:0] _T_1781;
  wire [31:0] _T_1782;
  wire [1:0] _T_1796;
  wire [1:0] _GEN_0;
  wire [2:0] _T_1797;
  wire  _T_1799;
  wire  _T_1801;
  wire  _T_1802;
  wire  _T_1809;
  wire  _T_1811;
  wire  _T_1812;
  wire  _T_1821;
  wire  _T_1823;
  ALUUnit alu (
    .clock(alu_clock),
    .reset(alu_reset),
    .io_req_valid(alu_io_req_valid),
    .io_req_bits_uop_valid(alu_io_req_bits_uop_valid),
    .io_req_bits_uop_iw_state(alu_io_req_bits_uop_iw_state),
    .io_req_bits_uop_uopc(alu_io_req_bits_uop_uopc),
    .io_req_bits_uop_inst(alu_io_req_bits_uop_inst),
    .io_req_bits_uop_pc(alu_io_req_bits_uop_pc),
    .io_req_bits_uop_iqtype(alu_io_req_bits_uop_iqtype),
    .io_req_bits_uop_fu_code(alu_io_req_bits_uop_fu_code),
    .io_req_bits_uop_ctrl_br_type(alu_io_req_bits_uop_ctrl_br_type),
    .io_req_bits_uop_ctrl_op1_sel(alu_io_req_bits_uop_ctrl_op1_sel),
    .io_req_bits_uop_ctrl_op2_sel(alu_io_req_bits_uop_ctrl_op2_sel),
    .io_req_bits_uop_ctrl_imm_sel(alu_io_req_bits_uop_ctrl_imm_sel),
    .io_req_bits_uop_ctrl_op_fcn(alu_io_req_bits_uop_ctrl_op_fcn),
    .io_req_bits_uop_ctrl_fcn_dw(alu_io_req_bits_uop_ctrl_fcn_dw),
    .io_req_bits_uop_ctrl_rf_wen(alu_io_req_bits_uop_ctrl_rf_wen),
    .io_req_bits_uop_ctrl_csr_cmd(alu_io_req_bits_uop_ctrl_csr_cmd),
    .io_req_bits_uop_ctrl_is_load(alu_io_req_bits_uop_ctrl_is_load),
    .io_req_bits_uop_ctrl_is_sta(alu_io_req_bits_uop_ctrl_is_sta),
    .io_req_bits_uop_ctrl_is_std(alu_io_req_bits_uop_ctrl_is_std),
    .io_req_bits_uop_allocate_brtag(alu_io_req_bits_uop_allocate_brtag),
    .io_req_bits_uop_is_br_or_jmp(alu_io_req_bits_uop_is_br_or_jmp),
    .io_req_bits_uop_is_jump(alu_io_req_bits_uop_is_jump),
    .io_req_bits_uop_is_jal(alu_io_req_bits_uop_is_jal),
    .io_req_bits_uop_is_ret(alu_io_req_bits_uop_is_ret),
    .io_req_bits_uop_is_call(alu_io_req_bits_uop_is_call),
    .io_req_bits_uop_br_mask(alu_io_req_bits_uop_br_mask),
    .io_req_bits_uop_br_tag(alu_io_req_bits_uop_br_tag),
    .io_req_bits_uop_br_prediction_btb_blame(alu_io_req_bits_uop_br_prediction_btb_blame),
    .io_req_bits_uop_br_prediction_btb_hit(alu_io_req_bits_uop_br_prediction_btb_hit),
    .io_req_bits_uop_br_prediction_btb_taken(alu_io_req_bits_uop_br_prediction_btb_taken),
    .io_req_bits_uop_br_prediction_bpd_blame(alu_io_req_bits_uop_br_prediction_bpd_blame),
    .io_req_bits_uop_br_prediction_bpd_hit(alu_io_req_bits_uop_br_prediction_bpd_hit),
    .io_req_bits_uop_br_prediction_bpd_taken(alu_io_req_bits_uop_br_prediction_bpd_taken),
    .io_req_bits_uop_br_prediction_bim_resp_value(alu_io_req_bits_uop_br_prediction_bim_resp_value),
    .io_req_bits_uop_br_prediction_bim_resp_entry_idx(alu_io_req_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_req_bits_uop_br_prediction_bim_resp_way_idx(alu_io_req_bits_uop_br_prediction_bim_resp_way_idx),
    .io_req_bits_uop_br_prediction_bpd_resp_takens(alu_io_req_bits_uop_br_prediction_bpd_resp_takens),
    .io_req_bits_uop_br_prediction_bpd_resp_history(alu_io_req_bits_uop_br_prediction_bpd_resp_history),
    .io_req_bits_uop_br_prediction_bpd_resp_history_u(alu_io_req_bits_uop_br_prediction_bpd_resp_history_u),
    .io_req_bits_uop_br_prediction_bpd_resp_history_ptr(alu_io_req_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_req_bits_uop_br_prediction_bpd_resp_info(alu_io_req_bits_uop_br_prediction_bpd_resp_info),
    .io_req_bits_uop_stat_brjmp_mispredicted(alu_io_req_bits_uop_stat_brjmp_mispredicted),
    .io_req_bits_uop_stat_btb_made_pred(alu_io_req_bits_uop_stat_btb_made_pred),
    .io_req_bits_uop_stat_btb_mispredicted(alu_io_req_bits_uop_stat_btb_mispredicted),
    .io_req_bits_uop_stat_bpd_made_pred(alu_io_req_bits_uop_stat_bpd_made_pred),
    .io_req_bits_uop_stat_bpd_mispredicted(alu_io_req_bits_uop_stat_bpd_mispredicted),
    .io_req_bits_uop_fetch_pc_lob(alu_io_req_bits_uop_fetch_pc_lob),
    .io_req_bits_uop_imm_packed(alu_io_req_bits_uop_imm_packed),
    .io_req_bits_uop_csr_addr(alu_io_req_bits_uop_csr_addr),
    .io_req_bits_uop_rob_idx(alu_io_req_bits_uop_rob_idx),
    .io_req_bits_uop_ldq_idx(alu_io_req_bits_uop_ldq_idx),
    .io_req_bits_uop_stq_idx(alu_io_req_bits_uop_stq_idx),
    .io_req_bits_uop_brob_idx(alu_io_req_bits_uop_brob_idx),
    .io_req_bits_uop_pdst(alu_io_req_bits_uop_pdst),
    .io_req_bits_uop_pop1(alu_io_req_bits_uop_pop1),
    .io_req_bits_uop_pop2(alu_io_req_bits_uop_pop2),
    .io_req_bits_uop_pop3(alu_io_req_bits_uop_pop3),
    .io_req_bits_uop_prs1_busy(alu_io_req_bits_uop_prs1_busy),
    .io_req_bits_uop_prs2_busy(alu_io_req_bits_uop_prs2_busy),
    .io_req_bits_uop_prs3_busy(alu_io_req_bits_uop_prs3_busy),
    .io_req_bits_uop_stale_pdst(alu_io_req_bits_uop_stale_pdst),
    .io_req_bits_uop_exception(alu_io_req_bits_uop_exception),
    .io_req_bits_uop_exc_cause(alu_io_req_bits_uop_exc_cause),
    .io_req_bits_uop_bypassable(alu_io_req_bits_uop_bypassable),
    .io_req_bits_uop_mem_cmd(alu_io_req_bits_uop_mem_cmd),
    .io_req_bits_uop_mem_typ(alu_io_req_bits_uop_mem_typ),
    .io_req_bits_uop_is_fence(alu_io_req_bits_uop_is_fence),
    .io_req_bits_uop_is_fencei(alu_io_req_bits_uop_is_fencei),
    .io_req_bits_uop_is_store(alu_io_req_bits_uop_is_store),
    .io_req_bits_uop_is_amo(alu_io_req_bits_uop_is_amo),
    .io_req_bits_uop_is_load(alu_io_req_bits_uop_is_load),
    .io_req_bits_uop_is_sys_pc2epc(alu_io_req_bits_uop_is_sys_pc2epc),
    .io_req_bits_uop_is_unique(alu_io_req_bits_uop_is_unique),
    .io_req_bits_uop_flush_on_commit(alu_io_req_bits_uop_flush_on_commit),
    .io_req_bits_uop_ldst(alu_io_req_bits_uop_ldst),
    .io_req_bits_uop_lrs1(alu_io_req_bits_uop_lrs1),
    .io_req_bits_uop_lrs2(alu_io_req_bits_uop_lrs2),
    .io_req_bits_uop_lrs3(alu_io_req_bits_uop_lrs3),
    .io_req_bits_uop_ldst_val(alu_io_req_bits_uop_ldst_val),
    .io_req_bits_uop_dst_rtype(alu_io_req_bits_uop_dst_rtype),
    .io_req_bits_uop_lrs1_rtype(alu_io_req_bits_uop_lrs1_rtype),
    .io_req_bits_uop_lrs2_rtype(alu_io_req_bits_uop_lrs2_rtype),
    .io_req_bits_uop_frs3_en(alu_io_req_bits_uop_frs3_en),
    .io_req_bits_uop_fp_val(alu_io_req_bits_uop_fp_val),
    .io_req_bits_uop_fp_single(alu_io_req_bits_uop_fp_single),
    .io_req_bits_uop_xcpt_pf_if(alu_io_req_bits_uop_xcpt_pf_if),
    .io_req_bits_uop_xcpt_ae_if(alu_io_req_bits_uop_xcpt_ae_if),
    .io_req_bits_uop_replay_if(alu_io_req_bits_uop_replay_if),
    .io_req_bits_uop_xcpt_ma_if(alu_io_req_bits_uop_xcpt_ma_if),
    .io_req_bits_uop_debug_wdata(alu_io_req_bits_uop_debug_wdata),
    .io_req_bits_uop_debug_events_fetch_seq(alu_io_req_bits_uop_debug_events_fetch_seq),
    .io_req_bits_rs1_data(alu_io_req_bits_rs1_data),
    .io_req_bits_rs2_data(alu_io_req_bits_rs2_data),
    .io_req_bits_kill(alu_io_req_bits_kill),
    .io_resp_valid(alu_io_resp_valid),
    .io_resp_bits_uop_valid(alu_io_resp_bits_uop_valid),
    .io_resp_bits_uop_iw_state(alu_io_resp_bits_uop_iw_state),
    .io_resp_bits_uop_uopc(alu_io_resp_bits_uop_uopc),
    .io_resp_bits_uop_inst(alu_io_resp_bits_uop_inst),
    .io_resp_bits_uop_pc(alu_io_resp_bits_uop_pc),
    .io_resp_bits_uop_iqtype(alu_io_resp_bits_uop_iqtype),
    .io_resp_bits_uop_fu_code(alu_io_resp_bits_uop_fu_code),
    .io_resp_bits_uop_ctrl_br_type(alu_io_resp_bits_uop_ctrl_br_type),
    .io_resp_bits_uop_ctrl_op1_sel(alu_io_resp_bits_uop_ctrl_op1_sel),
    .io_resp_bits_uop_ctrl_op2_sel(alu_io_resp_bits_uop_ctrl_op2_sel),
    .io_resp_bits_uop_ctrl_imm_sel(alu_io_resp_bits_uop_ctrl_imm_sel),
    .io_resp_bits_uop_ctrl_op_fcn(alu_io_resp_bits_uop_ctrl_op_fcn),
    .io_resp_bits_uop_ctrl_fcn_dw(alu_io_resp_bits_uop_ctrl_fcn_dw),
    .io_resp_bits_uop_ctrl_rf_wen(alu_io_resp_bits_uop_ctrl_rf_wen),
    .io_resp_bits_uop_ctrl_csr_cmd(alu_io_resp_bits_uop_ctrl_csr_cmd),
    .io_resp_bits_uop_ctrl_is_load(alu_io_resp_bits_uop_ctrl_is_load),
    .io_resp_bits_uop_ctrl_is_sta(alu_io_resp_bits_uop_ctrl_is_sta),
    .io_resp_bits_uop_ctrl_is_std(alu_io_resp_bits_uop_ctrl_is_std),
    .io_resp_bits_uop_allocate_brtag(alu_io_resp_bits_uop_allocate_brtag),
    .io_resp_bits_uop_is_br_or_jmp(alu_io_resp_bits_uop_is_br_or_jmp),
    .io_resp_bits_uop_is_jump(alu_io_resp_bits_uop_is_jump),
    .io_resp_bits_uop_is_jal(alu_io_resp_bits_uop_is_jal),
    .io_resp_bits_uop_is_ret(alu_io_resp_bits_uop_is_ret),
    .io_resp_bits_uop_is_call(alu_io_resp_bits_uop_is_call),
    .io_resp_bits_uop_br_mask(alu_io_resp_bits_uop_br_mask),
    .io_resp_bits_uop_br_tag(alu_io_resp_bits_uop_br_tag),
    .io_resp_bits_uop_br_prediction_btb_blame(alu_io_resp_bits_uop_br_prediction_btb_blame),
    .io_resp_bits_uop_br_prediction_btb_hit(alu_io_resp_bits_uop_br_prediction_btb_hit),
    .io_resp_bits_uop_br_prediction_btb_taken(alu_io_resp_bits_uop_br_prediction_btb_taken),
    .io_resp_bits_uop_br_prediction_bpd_blame(alu_io_resp_bits_uop_br_prediction_bpd_blame),
    .io_resp_bits_uop_br_prediction_bpd_hit(alu_io_resp_bits_uop_br_prediction_bpd_hit),
    .io_resp_bits_uop_br_prediction_bpd_taken(alu_io_resp_bits_uop_br_prediction_bpd_taken),
    .io_resp_bits_uop_br_prediction_bim_resp_value(alu_io_resp_bits_uop_br_prediction_bim_resp_value),
    .io_resp_bits_uop_br_prediction_bim_resp_entry_idx(alu_io_resp_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_resp_bits_uop_br_prediction_bim_resp_way_idx(alu_io_resp_bits_uop_br_prediction_bim_resp_way_idx),
    .io_resp_bits_uop_br_prediction_bpd_resp_takens(alu_io_resp_bits_uop_br_prediction_bpd_resp_takens),
    .io_resp_bits_uop_br_prediction_bpd_resp_history(alu_io_resp_bits_uop_br_prediction_bpd_resp_history),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_u(alu_io_resp_bits_uop_br_prediction_bpd_resp_history_u),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_ptr(alu_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_resp_bits_uop_br_prediction_bpd_resp_info(alu_io_resp_bits_uop_br_prediction_bpd_resp_info),
    .io_resp_bits_uop_stat_brjmp_mispredicted(alu_io_resp_bits_uop_stat_brjmp_mispredicted),
    .io_resp_bits_uop_stat_btb_made_pred(alu_io_resp_bits_uop_stat_btb_made_pred),
    .io_resp_bits_uop_stat_btb_mispredicted(alu_io_resp_bits_uop_stat_btb_mispredicted),
    .io_resp_bits_uop_stat_bpd_made_pred(alu_io_resp_bits_uop_stat_bpd_made_pred),
    .io_resp_bits_uop_stat_bpd_mispredicted(alu_io_resp_bits_uop_stat_bpd_mispredicted),
    .io_resp_bits_uop_fetch_pc_lob(alu_io_resp_bits_uop_fetch_pc_lob),
    .io_resp_bits_uop_imm_packed(alu_io_resp_bits_uop_imm_packed),
    .io_resp_bits_uop_csr_addr(alu_io_resp_bits_uop_csr_addr),
    .io_resp_bits_uop_rob_idx(alu_io_resp_bits_uop_rob_idx),
    .io_resp_bits_uop_ldq_idx(alu_io_resp_bits_uop_ldq_idx),
    .io_resp_bits_uop_stq_idx(alu_io_resp_bits_uop_stq_idx),
    .io_resp_bits_uop_brob_idx(alu_io_resp_bits_uop_brob_idx),
    .io_resp_bits_uop_pdst(alu_io_resp_bits_uop_pdst),
    .io_resp_bits_uop_pop1(alu_io_resp_bits_uop_pop1),
    .io_resp_bits_uop_pop2(alu_io_resp_bits_uop_pop2),
    .io_resp_bits_uop_pop3(alu_io_resp_bits_uop_pop3),
    .io_resp_bits_uop_prs1_busy(alu_io_resp_bits_uop_prs1_busy),
    .io_resp_bits_uop_prs2_busy(alu_io_resp_bits_uop_prs2_busy),
    .io_resp_bits_uop_prs3_busy(alu_io_resp_bits_uop_prs3_busy),
    .io_resp_bits_uop_stale_pdst(alu_io_resp_bits_uop_stale_pdst),
    .io_resp_bits_uop_exception(alu_io_resp_bits_uop_exception),
    .io_resp_bits_uop_exc_cause(alu_io_resp_bits_uop_exc_cause),
    .io_resp_bits_uop_bypassable(alu_io_resp_bits_uop_bypassable),
    .io_resp_bits_uop_mem_cmd(alu_io_resp_bits_uop_mem_cmd),
    .io_resp_bits_uop_mem_typ(alu_io_resp_bits_uop_mem_typ),
    .io_resp_bits_uop_is_fence(alu_io_resp_bits_uop_is_fence),
    .io_resp_bits_uop_is_fencei(alu_io_resp_bits_uop_is_fencei),
    .io_resp_bits_uop_is_store(alu_io_resp_bits_uop_is_store),
    .io_resp_bits_uop_is_amo(alu_io_resp_bits_uop_is_amo),
    .io_resp_bits_uop_is_load(alu_io_resp_bits_uop_is_load),
    .io_resp_bits_uop_is_sys_pc2epc(alu_io_resp_bits_uop_is_sys_pc2epc),
    .io_resp_bits_uop_is_unique(alu_io_resp_bits_uop_is_unique),
    .io_resp_bits_uop_flush_on_commit(alu_io_resp_bits_uop_flush_on_commit),
    .io_resp_bits_uop_ldst(alu_io_resp_bits_uop_ldst),
    .io_resp_bits_uop_lrs1(alu_io_resp_bits_uop_lrs1),
    .io_resp_bits_uop_lrs2(alu_io_resp_bits_uop_lrs2),
    .io_resp_bits_uop_lrs3(alu_io_resp_bits_uop_lrs3),
    .io_resp_bits_uop_ldst_val(alu_io_resp_bits_uop_ldst_val),
    .io_resp_bits_uop_dst_rtype(alu_io_resp_bits_uop_dst_rtype),
    .io_resp_bits_uop_lrs1_rtype(alu_io_resp_bits_uop_lrs1_rtype),
    .io_resp_bits_uop_lrs2_rtype(alu_io_resp_bits_uop_lrs2_rtype),
    .io_resp_bits_uop_frs3_en(alu_io_resp_bits_uop_frs3_en),
    .io_resp_bits_uop_fp_val(alu_io_resp_bits_uop_fp_val),
    .io_resp_bits_uop_fp_single(alu_io_resp_bits_uop_fp_single),
    .io_resp_bits_uop_xcpt_pf_if(alu_io_resp_bits_uop_xcpt_pf_if),
    .io_resp_bits_uop_xcpt_ae_if(alu_io_resp_bits_uop_xcpt_ae_if),
    .io_resp_bits_uop_replay_if(alu_io_resp_bits_uop_replay_if),
    .io_resp_bits_uop_xcpt_ma_if(alu_io_resp_bits_uop_xcpt_ma_if),
    .io_resp_bits_uop_debug_wdata(alu_io_resp_bits_uop_debug_wdata),
    .io_resp_bits_uop_debug_events_fetch_seq(alu_io_resp_bits_uop_debug_events_fetch_seq),
    .io_resp_bits_data(alu_io_resp_bits_data),
    .io_brinfo_valid(alu_io_brinfo_valid),
    .io_brinfo_mispredict(alu_io_brinfo_mispredict),
    .io_brinfo_mask(alu_io_brinfo_mask),
    .io_bypass_valid_0(alu_io_bypass_valid_0),
    .io_bypass_valid_1(alu_io_bypass_valid_1),
    .io_bypass_valid_2(alu_io_bypass_valid_2),
    .io_bypass_uop_0_ctrl_rf_wen(alu_io_bypass_uop_0_ctrl_rf_wen),
    .io_bypass_uop_0_pdst(alu_io_bypass_uop_0_pdst),
    .io_bypass_uop_0_dst_rtype(alu_io_bypass_uop_0_dst_rtype),
    .io_bypass_uop_1_ctrl_rf_wen(alu_io_bypass_uop_1_ctrl_rf_wen),
    .io_bypass_uop_1_pdst(alu_io_bypass_uop_1_pdst),
    .io_bypass_uop_1_dst_rtype(alu_io_bypass_uop_1_dst_rtype),
    .io_bypass_uop_2_ctrl_rf_wen(alu_io_bypass_uop_2_ctrl_rf_wen),
    .io_bypass_uop_2_pdst(alu_io_bypass_uop_2_pdst),
    .io_bypass_uop_2_dst_rtype(alu_io_bypass_uop_2_dst_rtype),
    .io_bypass_data_0(alu_io_bypass_data_0),
    .io_bypass_data_1(alu_io_bypass_data_1),
    .io_bypass_data_2(alu_io_bypass_data_2),
    .io_br_unit_take_pc(alu_io_br_unit_take_pc),
    .io_br_unit_target(alu_io_br_unit_target),
    .io_br_unit_brinfo_valid(alu_io_br_unit_brinfo_valid),
    .io_br_unit_brinfo_mispredict(alu_io_br_unit_brinfo_mispredict),
    .io_br_unit_brinfo_mask(alu_io_br_unit_brinfo_mask),
    .io_br_unit_brinfo_tag(alu_io_br_unit_brinfo_tag),
    .io_br_unit_brinfo_exe_mask(alu_io_br_unit_brinfo_exe_mask),
    .io_br_unit_brinfo_rob_idx(alu_io_br_unit_brinfo_rob_idx),
    .io_br_unit_brinfo_ldq_idx(alu_io_br_unit_brinfo_ldq_idx),
    .io_br_unit_brinfo_stq_idx(alu_io_br_unit_brinfo_stq_idx),
    .io_br_unit_brinfo_is_jr(alu_io_br_unit_brinfo_is_jr),
    .io_br_unit_btb_update_valid(alu_io_br_unit_btb_update_valid),
    .io_br_unit_btb_update_bits_pc(alu_io_br_unit_btb_update_bits_pc),
    .io_br_unit_btb_update_bits_target(alu_io_br_unit_btb_update_bits_target),
    .io_br_unit_btb_update_bits_cfi_pc(alu_io_br_unit_btb_update_bits_cfi_pc),
    .io_br_unit_btb_update_bits_bpd_type(alu_io_br_unit_btb_update_bits_bpd_type),
    .io_br_unit_btb_update_bits_cfi_type(alu_io_br_unit_btb_update_bits_cfi_type),
    .io_br_unit_bim_update_valid(alu_io_br_unit_bim_update_valid),
    .io_br_unit_bim_update_bits_taken(alu_io_br_unit_bim_update_bits_taken),
    .io_br_unit_bim_update_bits_bim_resp_value(alu_io_br_unit_bim_update_bits_bim_resp_value),
    .io_br_unit_bim_update_bits_bim_resp_entry_idx(alu_io_br_unit_bim_update_bits_bim_resp_entry_idx),
    .io_br_unit_bim_update_bits_bim_resp_way_idx(alu_io_br_unit_bim_update_bits_bim_resp_way_idx),
    .io_br_unit_bpd_update_valid(alu_io_br_unit_bpd_update_valid),
    .io_br_unit_bpd_update_bits_br_pc(alu_io_br_unit_bpd_update_bits_br_pc),
    .io_br_unit_bpd_update_bits_brob_idx(alu_io_br_unit_bpd_update_bits_brob_idx),
    .io_br_unit_bpd_update_bits_mispredict(alu_io_br_unit_bpd_update_bits_mispredict),
    .io_br_unit_bpd_update_bits_history(alu_io_br_unit_bpd_update_bits_history),
    .io_br_unit_bpd_update_bits_history_ptr(alu_io_br_unit_bpd_update_bits_history_ptr),
    .io_br_unit_bpd_update_bits_bpd_predict_val(alu_io_br_unit_bpd_update_bits_bpd_predict_val),
    .io_br_unit_bpd_update_bits_bpd_mispredict(alu_io_br_unit_bpd_update_bits_bpd_mispredict),
    .io_br_unit_bpd_update_bits_taken(alu_io_br_unit_bpd_update_bits_taken),
    .io_br_unit_bpd_update_bits_is_br(alu_io_br_unit_bpd_update_bits_is_br),
    .io_br_unit_bpd_update_bits_new_pc_same_packet(alu_io_br_unit_bpd_update_bits_new_pc_same_packet),
    .io_br_unit_xcpt_valid(alu_io_br_unit_xcpt_valid),
    .io_br_unit_xcpt_bits_uop_br_mask(alu_io_br_unit_xcpt_bits_uop_br_mask),
    .io_br_unit_xcpt_bits_uop_rob_idx(alu_io_br_unit_xcpt_bits_uop_rob_idx),
    .io_br_unit_xcpt_bits_badvaddr(alu_io_br_unit_xcpt_bits_badvaddr),
    .io_get_rob_pc_curr_pc(alu_io_get_rob_pc_curr_pc),
    .io_get_rob_pc_curr_brob_idx(alu_io_get_rob_pc_curr_brob_idx),
    .io_get_rob_pc_next_val(alu_io_get_rob_pc_next_val),
    .io_get_rob_pc_next_pc(alu_io_get_rob_pc_next_pc),
    .io_get_pred_info_bim_resp_value(alu_io_get_pred_info_bim_resp_value),
    .io_get_pred_info_bim_resp_entry_idx(alu_io_get_pred_info_bim_resp_entry_idx),
    .io_get_pred_info_bim_resp_way_idx(alu_io_get_pred_info_bim_resp_way_idx),
    .io_get_pred_info_bpd_resp_history(alu_io_get_pred_info_bpd_resp_history),
    .io_get_pred_info_bpd_resp_history_ptr(alu_io_get_pred_info_bpd_resp_history_ptr),
    .io_status_debug(alu_io_status_debug)
  );
  PipelinedMulUnit fu_units_1 (
    .clock(fu_units_1_clock),
    .reset(fu_units_1_reset),
    .io_req_valid(fu_units_1_io_req_valid),
    .io_req_bits_uop_valid(fu_units_1_io_req_bits_uop_valid),
    .io_req_bits_uop_iw_state(fu_units_1_io_req_bits_uop_iw_state),
    .io_req_bits_uop_uopc(fu_units_1_io_req_bits_uop_uopc),
    .io_req_bits_uop_inst(fu_units_1_io_req_bits_uop_inst),
    .io_req_bits_uop_pc(fu_units_1_io_req_bits_uop_pc),
    .io_req_bits_uop_iqtype(fu_units_1_io_req_bits_uop_iqtype),
    .io_req_bits_uop_fu_code(fu_units_1_io_req_bits_uop_fu_code),
    .io_req_bits_uop_ctrl_br_type(fu_units_1_io_req_bits_uop_ctrl_br_type),
    .io_req_bits_uop_ctrl_op1_sel(fu_units_1_io_req_bits_uop_ctrl_op1_sel),
    .io_req_bits_uop_ctrl_op2_sel(fu_units_1_io_req_bits_uop_ctrl_op2_sel),
    .io_req_bits_uop_ctrl_imm_sel(fu_units_1_io_req_bits_uop_ctrl_imm_sel),
    .io_req_bits_uop_ctrl_op_fcn(fu_units_1_io_req_bits_uop_ctrl_op_fcn),
    .io_req_bits_uop_ctrl_fcn_dw(fu_units_1_io_req_bits_uop_ctrl_fcn_dw),
    .io_req_bits_uop_ctrl_rf_wen(fu_units_1_io_req_bits_uop_ctrl_rf_wen),
    .io_req_bits_uop_ctrl_csr_cmd(fu_units_1_io_req_bits_uop_ctrl_csr_cmd),
    .io_req_bits_uop_ctrl_is_load(fu_units_1_io_req_bits_uop_ctrl_is_load),
    .io_req_bits_uop_ctrl_is_sta(fu_units_1_io_req_bits_uop_ctrl_is_sta),
    .io_req_bits_uop_ctrl_is_std(fu_units_1_io_req_bits_uop_ctrl_is_std),
    .io_req_bits_uop_allocate_brtag(fu_units_1_io_req_bits_uop_allocate_brtag),
    .io_req_bits_uop_is_br_or_jmp(fu_units_1_io_req_bits_uop_is_br_or_jmp),
    .io_req_bits_uop_is_jump(fu_units_1_io_req_bits_uop_is_jump),
    .io_req_bits_uop_is_jal(fu_units_1_io_req_bits_uop_is_jal),
    .io_req_bits_uop_is_ret(fu_units_1_io_req_bits_uop_is_ret),
    .io_req_bits_uop_is_call(fu_units_1_io_req_bits_uop_is_call),
    .io_req_bits_uop_br_mask(fu_units_1_io_req_bits_uop_br_mask),
    .io_req_bits_uop_br_tag(fu_units_1_io_req_bits_uop_br_tag),
    .io_req_bits_uop_br_prediction_btb_blame(fu_units_1_io_req_bits_uop_br_prediction_btb_blame),
    .io_req_bits_uop_br_prediction_btb_hit(fu_units_1_io_req_bits_uop_br_prediction_btb_hit),
    .io_req_bits_uop_br_prediction_btb_taken(fu_units_1_io_req_bits_uop_br_prediction_btb_taken),
    .io_req_bits_uop_br_prediction_bpd_blame(fu_units_1_io_req_bits_uop_br_prediction_bpd_blame),
    .io_req_bits_uop_br_prediction_bpd_hit(fu_units_1_io_req_bits_uop_br_prediction_bpd_hit),
    .io_req_bits_uop_br_prediction_bpd_taken(fu_units_1_io_req_bits_uop_br_prediction_bpd_taken),
    .io_req_bits_uop_br_prediction_bim_resp_value(fu_units_1_io_req_bits_uop_br_prediction_bim_resp_value),
    .io_req_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_1_io_req_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_req_bits_uop_br_prediction_bim_resp_way_idx(fu_units_1_io_req_bits_uop_br_prediction_bim_resp_way_idx),
    .io_req_bits_uop_br_prediction_bpd_resp_takens(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_takens),
    .io_req_bits_uop_br_prediction_bpd_resp_history(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history),
    .io_req_bits_uop_br_prediction_bpd_resp_history_u(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_u),
    .io_req_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_req_bits_uop_br_prediction_bpd_resp_info(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_info),
    .io_req_bits_uop_stat_brjmp_mispredicted(fu_units_1_io_req_bits_uop_stat_brjmp_mispredicted),
    .io_req_bits_uop_stat_btb_made_pred(fu_units_1_io_req_bits_uop_stat_btb_made_pred),
    .io_req_bits_uop_stat_btb_mispredicted(fu_units_1_io_req_bits_uop_stat_btb_mispredicted),
    .io_req_bits_uop_stat_bpd_made_pred(fu_units_1_io_req_bits_uop_stat_bpd_made_pred),
    .io_req_bits_uop_stat_bpd_mispredicted(fu_units_1_io_req_bits_uop_stat_bpd_mispredicted),
    .io_req_bits_uop_fetch_pc_lob(fu_units_1_io_req_bits_uop_fetch_pc_lob),
    .io_req_bits_uop_imm_packed(fu_units_1_io_req_bits_uop_imm_packed),
    .io_req_bits_uop_csr_addr(fu_units_1_io_req_bits_uop_csr_addr),
    .io_req_bits_uop_rob_idx(fu_units_1_io_req_bits_uop_rob_idx),
    .io_req_bits_uop_ldq_idx(fu_units_1_io_req_bits_uop_ldq_idx),
    .io_req_bits_uop_stq_idx(fu_units_1_io_req_bits_uop_stq_idx),
    .io_req_bits_uop_brob_idx(fu_units_1_io_req_bits_uop_brob_idx),
    .io_req_bits_uop_pdst(fu_units_1_io_req_bits_uop_pdst),
    .io_req_bits_uop_pop1(fu_units_1_io_req_bits_uop_pop1),
    .io_req_bits_uop_pop2(fu_units_1_io_req_bits_uop_pop2),
    .io_req_bits_uop_pop3(fu_units_1_io_req_bits_uop_pop3),
    .io_req_bits_uop_prs1_busy(fu_units_1_io_req_bits_uop_prs1_busy),
    .io_req_bits_uop_prs2_busy(fu_units_1_io_req_bits_uop_prs2_busy),
    .io_req_bits_uop_prs3_busy(fu_units_1_io_req_bits_uop_prs3_busy),
    .io_req_bits_uop_stale_pdst(fu_units_1_io_req_bits_uop_stale_pdst),
    .io_req_bits_uop_exception(fu_units_1_io_req_bits_uop_exception),
    .io_req_bits_uop_exc_cause(fu_units_1_io_req_bits_uop_exc_cause),
    .io_req_bits_uop_bypassable(fu_units_1_io_req_bits_uop_bypassable),
    .io_req_bits_uop_mem_cmd(fu_units_1_io_req_bits_uop_mem_cmd),
    .io_req_bits_uop_mem_typ(fu_units_1_io_req_bits_uop_mem_typ),
    .io_req_bits_uop_is_fence(fu_units_1_io_req_bits_uop_is_fence),
    .io_req_bits_uop_is_fencei(fu_units_1_io_req_bits_uop_is_fencei),
    .io_req_bits_uop_is_store(fu_units_1_io_req_bits_uop_is_store),
    .io_req_bits_uop_is_amo(fu_units_1_io_req_bits_uop_is_amo),
    .io_req_bits_uop_is_load(fu_units_1_io_req_bits_uop_is_load),
    .io_req_bits_uop_is_sys_pc2epc(fu_units_1_io_req_bits_uop_is_sys_pc2epc),
    .io_req_bits_uop_is_unique(fu_units_1_io_req_bits_uop_is_unique),
    .io_req_bits_uop_flush_on_commit(fu_units_1_io_req_bits_uop_flush_on_commit),
    .io_req_bits_uop_ldst(fu_units_1_io_req_bits_uop_ldst),
    .io_req_bits_uop_lrs1(fu_units_1_io_req_bits_uop_lrs1),
    .io_req_bits_uop_lrs2(fu_units_1_io_req_bits_uop_lrs2),
    .io_req_bits_uop_lrs3(fu_units_1_io_req_bits_uop_lrs3),
    .io_req_bits_uop_ldst_val(fu_units_1_io_req_bits_uop_ldst_val),
    .io_req_bits_uop_dst_rtype(fu_units_1_io_req_bits_uop_dst_rtype),
    .io_req_bits_uop_lrs1_rtype(fu_units_1_io_req_bits_uop_lrs1_rtype),
    .io_req_bits_uop_lrs2_rtype(fu_units_1_io_req_bits_uop_lrs2_rtype),
    .io_req_bits_uop_frs3_en(fu_units_1_io_req_bits_uop_frs3_en),
    .io_req_bits_uop_fp_val(fu_units_1_io_req_bits_uop_fp_val),
    .io_req_bits_uop_fp_single(fu_units_1_io_req_bits_uop_fp_single),
    .io_req_bits_uop_xcpt_pf_if(fu_units_1_io_req_bits_uop_xcpt_pf_if),
    .io_req_bits_uop_xcpt_ae_if(fu_units_1_io_req_bits_uop_xcpt_ae_if),
    .io_req_bits_uop_replay_if(fu_units_1_io_req_bits_uop_replay_if),
    .io_req_bits_uop_xcpt_ma_if(fu_units_1_io_req_bits_uop_xcpt_ma_if),
    .io_req_bits_uop_debug_wdata(fu_units_1_io_req_bits_uop_debug_wdata),
    .io_req_bits_uop_debug_events_fetch_seq(fu_units_1_io_req_bits_uop_debug_events_fetch_seq),
    .io_req_bits_rs1_data(fu_units_1_io_req_bits_rs1_data),
    .io_req_bits_rs2_data(fu_units_1_io_req_bits_rs2_data),
    .io_req_bits_kill(fu_units_1_io_req_bits_kill),
    .io_resp_valid(fu_units_1_io_resp_valid),
    .io_resp_bits_uop_valid(fu_units_1_io_resp_bits_uop_valid),
    .io_resp_bits_uop_iw_state(fu_units_1_io_resp_bits_uop_iw_state),
    .io_resp_bits_uop_uopc(fu_units_1_io_resp_bits_uop_uopc),
    .io_resp_bits_uop_inst(fu_units_1_io_resp_bits_uop_inst),
    .io_resp_bits_uop_pc(fu_units_1_io_resp_bits_uop_pc),
    .io_resp_bits_uop_iqtype(fu_units_1_io_resp_bits_uop_iqtype),
    .io_resp_bits_uop_fu_code(fu_units_1_io_resp_bits_uop_fu_code),
    .io_resp_bits_uop_ctrl_br_type(fu_units_1_io_resp_bits_uop_ctrl_br_type),
    .io_resp_bits_uop_ctrl_op1_sel(fu_units_1_io_resp_bits_uop_ctrl_op1_sel),
    .io_resp_bits_uop_ctrl_op2_sel(fu_units_1_io_resp_bits_uop_ctrl_op2_sel),
    .io_resp_bits_uop_ctrl_imm_sel(fu_units_1_io_resp_bits_uop_ctrl_imm_sel),
    .io_resp_bits_uop_ctrl_op_fcn(fu_units_1_io_resp_bits_uop_ctrl_op_fcn),
    .io_resp_bits_uop_ctrl_fcn_dw(fu_units_1_io_resp_bits_uop_ctrl_fcn_dw),
    .io_resp_bits_uop_ctrl_rf_wen(fu_units_1_io_resp_bits_uop_ctrl_rf_wen),
    .io_resp_bits_uop_ctrl_csr_cmd(fu_units_1_io_resp_bits_uop_ctrl_csr_cmd),
    .io_resp_bits_uop_ctrl_is_load(fu_units_1_io_resp_bits_uop_ctrl_is_load),
    .io_resp_bits_uop_ctrl_is_sta(fu_units_1_io_resp_bits_uop_ctrl_is_sta),
    .io_resp_bits_uop_ctrl_is_std(fu_units_1_io_resp_bits_uop_ctrl_is_std),
    .io_resp_bits_uop_allocate_brtag(fu_units_1_io_resp_bits_uop_allocate_brtag),
    .io_resp_bits_uop_is_br_or_jmp(fu_units_1_io_resp_bits_uop_is_br_or_jmp),
    .io_resp_bits_uop_is_jump(fu_units_1_io_resp_bits_uop_is_jump),
    .io_resp_bits_uop_is_jal(fu_units_1_io_resp_bits_uop_is_jal),
    .io_resp_bits_uop_is_ret(fu_units_1_io_resp_bits_uop_is_ret),
    .io_resp_bits_uop_is_call(fu_units_1_io_resp_bits_uop_is_call),
    .io_resp_bits_uop_br_mask(fu_units_1_io_resp_bits_uop_br_mask),
    .io_resp_bits_uop_br_tag(fu_units_1_io_resp_bits_uop_br_tag),
    .io_resp_bits_uop_br_prediction_btb_blame(fu_units_1_io_resp_bits_uop_br_prediction_btb_blame),
    .io_resp_bits_uop_br_prediction_btb_hit(fu_units_1_io_resp_bits_uop_br_prediction_btb_hit),
    .io_resp_bits_uop_br_prediction_btb_taken(fu_units_1_io_resp_bits_uop_br_prediction_btb_taken),
    .io_resp_bits_uop_br_prediction_bpd_blame(fu_units_1_io_resp_bits_uop_br_prediction_bpd_blame),
    .io_resp_bits_uop_br_prediction_bpd_hit(fu_units_1_io_resp_bits_uop_br_prediction_bpd_hit),
    .io_resp_bits_uop_br_prediction_bpd_taken(fu_units_1_io_resp_bits_uop_br_prediction_bpd_taken),
    .io_resp_bits_uop_br_prediction_bim_resp_value(fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_value),
    .io_resp_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_resp_bits_uop_br_prediction_bim_resp_way_idx(fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_way_idx),
    .io_resp_bits_uop_br_prediction_bpd_resp_takens(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_takens),
    .io_resp_bits_uop_br_prediction_bpd_resp_history(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_u(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_u),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_resp_bits_uop_br_prediction_bpd_resp_info(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_info),
    .io_resp_bits_uop_stat_brjmp_mispredicted(fu_units_1_io_resp_bits_uop_stat_brjmp_mispredicted),
    .io_resp_bits_uop_stat_btb_made_pred(fu_units_1_io_resp_bits_uop_stat_btb_made_pred),
    .io_resp_bits_uop_stat_btb_mispredicted(fu_units_1_io_resp_bits_uop_stat_btb_mispredicted),
    .io_resp_bits_uop_stat_bpd_made_pred(fu_units_1_io_resp_bits_uop_stat_bpd_made_pred),
    .io_resp_bits_uop_stat_bpd_mispredicted(fu_units_1_io_resp_bits_uop_stat_bpd_mispredicted),
    .io_resp_bits_uop_fetch_pc_lob(fu_units_1_io_resp_bits_uop_fetch_pc_lob),
    .io_resp_bits_uop_imm_packed(fu_units_1_io_resp_bits_uop_imm_packed),
    .io_resp_bits_uop_csr_addr(fu_units_1_io_resp_bits_uop_csr_addr),
    .io_resp_bits_uop_rob_idx(fu_units_1_io_resp_bits_uop_rob_idx),
    .io_resp_bits_uop_ldq_idx(fu_units_1_io_resp_bits_uop_ldq_idx),
    .io_resp_bits_uop_stq_idx(fu_units_1_io_resp_bits_uop_stq_idx),
    .io_resp_bits_uop_brob_idx(fu_units_1_io_resp_bits_uop_brob_idx),
    .io_resp_bits_uop_pdst(fu_units_1_io_resp_bits_uop_pdst),
    .io_resp_bits_uop_pop1(fu_units_1_io_resp_bits_uop_pop1),
    .io_resp_bits_uop_pop2(fu_units_1_io_resp_bits_uop_pop2),
    .io_resp_bits_uop_pop3(fu_units_1_io_resp_bits_uop_pop3),
    .io_resp_bits_uop_prs1_busy(fu_units_1_io_resp_bits_uop_prs1_busy),
    .io_resp_bits_uop_prs2_busy(fu_units_1_io_resp_bits_uop_prs2_busy),
    .io_resp_bits_uop_prs3_busy(fu_units_1_io_resp_bits_uop_prs3_busy),
    .io_resp_bits_uop_stale_pdst(fu_units_1_io_resp_bits_uop_stale_pdst),
    .io_resp_bits_uop_exception(fu_units_1_io_resp_bits_uop_exception),
    .io_resp_bits_uop_exc_cause(fu_units_1_io_resp_bits_uop_exc_cause),
    .io_resp_bits_uop_bypassable(fu_units_1_io_resp_bits_uop_bypassable),
    .io_resp_bits_uop_mem_cmd(fu_units_1_io_resp_bits_uop_mem_cmd),
    .io_resp_bits_uop_mem_typ(fu_units_1_io_resp_bits_uop_mem_typ),
    .io_resp_bits_uop_is_fence(fu_units_1_io_resp_bits_uop_is_fence),
    .io_resp_bits_uop_is_fencei(fu_units_1_io_resp_bits_uop_is_fencei),
    .io_resp_bits_uop_is_store(fu_units_1_io_resp_bits_uop_is_store),
    .io_resp_bits_uop_is_amo(fu_units_1_io_resp_bits_uop_is_amo),
    .io_resp_bits_uop_is_load(fu_units_1_io_resp_bits_uop_is_load),
    .io_resp_bits_uop_is_sys_pc2epc(fu_units_1_io_resp_bits_uop_is_sys_pc2epc),
    .io_resp_bits_uop_is_unique(fu_units_1_io_resp_bits_uop_is_unique),
    .io_resp_bits_uop_flush_on_commit(fu_units_1_io_resp_bits_uop_flush_on_commit),
    .io_resp_bits_uop_ldst(fu_units_1_io_resp_bits_uop_ldst),
    .io_resp_bits_uop_lrs1(fu_units_1_io_resp_bits_uop_lrs1),
    .io_resp_bits_uop_lrs2(fu_units_1_io_resp_bits_uop_lrs2),
    .io_resp_bits_uop_lrs3(fu_units_1_io_resp_bits_uop_lrs3),
    .io_resp_bits_uop_ldst_val(fu_units_1_io_resp_bits_uop_ldst_val),
    .io_resp_bits_uop_dst_rtype(fu_units_1_io_resp_bits_uop_dst_rtype),
    .io_resp_bits_uop_lrs1_rtype(fu_units_1_io_resp_bits_uop_lrs1_rtype),
    .io_resp_bits_uop_lrs2_rtype(fu_units_1_io_resp_bits_uop_lrs2_rtype),
    .io_resp_bits_uop_frs3_en(fu_units_1_io_resp_bits_uop_frs3_en),
    .io_resp_bits_uop_fp_val(fu_units_1_io_resp_bits_uop_fp_val),
    .io_resp_bits_uop_fp_single(fu_units_1_io_resp_bits_uop_fp_single),
    .io_resp_bits_uop_xcpt_pf_if(fu_units_1_io_resp_bits_uop_xcpt_pf_if),
    .io_resp_bits_uop_xcpt_ae_if(fu_units_1_io_resp_bits_uop_xcpt_ae_if),
    .io_resp_bits_uop_replay_if(fu_units_1_io_resp_bits_uop_replay_if),
    .io_resp_bits_uop_xcpt_ma_if(fu_units_1_io_resp_bits_uop_xcpt_ma_if),
    .io_resp_bits_uop_debug_wdata(fu_units_1_io_resp_bits_uop_debug_wdata),
    .io_resp_bits_uop_debug_events_fetch_seq(fu_units_1_io_resp_bits_uop_debug_events_fetch_seq),
    .io_resp_bits_data(fu_units_1_io_resp_bits_data),
    .io_brinfo_valid(fu_units_1_io_brinfo_valid),
    .io_brinfo_mispredict(fu_units_1_io_brinfo_mispredict),
    .io_brinfo_mask(fu_units_1_io_brinfo_mask)
  );
  local_MulDivUnit fu_units_2 (
    .clock(fu_units_2_clock),
    .reset(fu_units_2_reset),
    .io_req_ready(fu_units_2_io_req_ready),
    .io_req_valid(fu_units_2_io_req_valid),
    .io_req_bits_uop_valid(fu_units_2_io_req_bits_uop_valid),
    .io_req_bits_uop_iw_state(fu_units_2_io_req_bits_uop_iw_state),
    .io_req_bits_uop_uopc(fu_units_2_io_req_bits_uop_uopc),
    .io_req_bits_uop_inst(fu_units_2_io_req_bits_uop_inst),
    .io_req_bits_uop_pc(fu_units_2_io_req_bits_uop_pc),
    .io_req_bits_uop_iqtype(fu_units_2_io_req_bits_uop_iqtype),
    .io_req_bits_uop_fu_code(fu_units_2_io_req_bits_uop_fu_code),
    .io_req_bits_uop_ctrl_br_type(fu_units_2_io_req_bits_uop_ctrl_br_type),
    .io_req_bits_uop_ctrl_op1_sel(fu_units_2_io_req_bits_uop_ctrl_op1_sel),
    .io_req_bits_uop_ctrl_op2_sel(fu_units_2_io_req_bits_uop_ctrl_op2_sel),
    .io_req_bits_uop_ctrl_imm_sel(fu_units_2_io_req_bits_uop_ctrl_imm_sel),
    .io_req_bits_uop_ctrl_op_fcn(fu_units_2_io_req_bits_uop_ctrl_op_fcn),
    .io_req_bits_uop_ctrl_fcn_dw(fu_units_2_io_req_bits_uop_ctrl_fcn_dw),
    .io_req_bits_uop_ctrl_rf_wen(fu_units_2_io_req_bits_uop_ctrl_rf_wen),
    .io_req_bits_uop_ctrl_csr_cmd(fu_units_2_io_req_bits_uop_ctrl_csr_cmd),
    .io_req_bits_uop_ctrl_is_load(fu_units_2_io_req_bits_uop_ctrl_is_load),
    .io_req_bits_uop_ctrl_is_sta(fu_units_2_io_req_bits_uop_ctrl_is_sta),
    .io_req_bits_uop_ctrl_is_std(fu_units_2_io_req_bits_uop_ctrl_is_std),
    .io_req_bits_uop_allocate_brtag(fu_units_2_io_req_bits_uop_allocate_brtag),
    .io_req_bits_uop_is_br_or_jmp(fu_units_2_io_req_bits_uop_is_br_or_jmp),
    .io_req_bits_uop_is_jump(fu_units_2_io_req_bits_uop_is_jump),
    .io_req_bits_uop_is_jal(fu_units_2_io_req_bits_uop_is_jal),
    .io_req_bits_uop_is_ret(fu_units_2_io_req_bits_uop_is_ret),
    .io_req_bits_uop_is_call(fu_units_2_io_req_bits_uop_is_call),
    .io_req_bits_uop_br_mask(fu_units_2_io_req_bits_uop_br_mask),
    .io_req_bits_uop_br_tag(fu_units_2_io_req_bits_uop_br_tag),
    .io_req_bits_uop_br_prediction_btb_blame(fu_units_2_io_req_bits_uop_br_prediction_btb_blame),
    .io_req_bits_uop_br_prediction_btb_hit(fu_units_2_io_req_bits_uop_br_prediction_btb_hit),
    .io_req_bits_uop_br_prediction_btb_taken(fu_units_2_io_req_bits_uop_br_prediction_btb_taken),
    .io_req_bits_uop_br_prediction_bpd_blame(fu_units_2_io_req_bits_uop_br_prediction_bpd_blame),
    .io_req_bits_uop_br_prediction_bpd_hit(fu_units_2_io_req_bits_uop_br_prediction_bpd_hit),
    .io_req_bits_uop_br_prediction_bpd_taken(fu_units_2_io_req_bits_uop_br_prediction_bpd_taken),
    .io_req_bits_uop_br_prediction_bim_resp_value(fu_units_2_io_req_bits_uop_br_prediction_bim_resp_value),
    .io_req_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_2_io_req_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_req_bits_uop_br_prediction_bim_resp_way_idx(fu_units_2_io_req_bits_uop_br_prediction_bim_resp_way_idx),
    .io_req_bits_uop_br_prediction_bpd_resp_takens(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_takens),
    .io_req_bits_uop_br_prediction_bpd_resp_history(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history),
    .io_req_bits_uop_br_prediction_bpd_resp_history_u(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_u),
    .io_req_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_req_bits_uop_br_prediction_bpd_resp_info(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_info),
    .io_req_bits_uop_stat_brjmp_mispredicted(fu_units_2_io_req_bits_uop_stat_brjmp_mispredicted),
    .io_req_bits_uop_stat_btb_made_pred(fu_units_2_io_req_bits_uop_stat_btb_made_pred),
    .io_req_bits_uop_stat_btb_mispredicted(fu_units_2_io_req_bits_uop_stat_btb_mispredicted),
    .io_req_bits_uop_stat_bpd_made_pred(fu_units_2_io_req_bits_uop_stat_bpd_made_pred),
    .io_req_bits_uop_stat_bpd_mispredicted(fu_units_2_io_req_bits_uop_stat_bpd_mispredicted),
    .io_req_bits_uop_fetch_pc_lob(fu_units_2_io_req_bits_uop_fetch_pc_lob),
    .io_req_bits_uop_imm_packed(fu_units_2_io_req_bits_uop_imm_packed),
    .io_req_bits_uop_csr_addr(fu_units_2_io_req_bits_uop_csr_addr),
    .io_req_bits_uop_rob_idx(fu_units_2_io_req_bits_uop_rob_idx),
    .io_req_bits_uop_ldq_idx(fu_units_2_io_req_bits_uop_ldq_idx),
    .io_req_bits_uop_stq_idx(fu_units_2_io_req_bits_uop_stq_idx),
    .io_req_bits_uop_brob_idx(fu_units_2_io_req_bits_uop_brob_idx),
    .io_req_bits_uop_pdst(fu_units_2_io_req_bits_uop_pdst),
    .io_req_bits_uop_pop1(fu_units_2_io_req_bits_uop_pop1),
    .io_req_bits_uop_pop2(fu_units_2_io_req_bits_uop_pop2),
    .io_req_bits_uop_pop3(fu_units_2_io_req_bits_uop_pop3),
    .io_req_bits_uop_prs1_busy(fu_units_2_io_req_bits_uop_prs1_busy),
    .io_req_bits_uop_prs2_busy(fu_units_2_io_req_bits_uop_prs2_busy),
    .io_req_bits_uop_prs3_busy(fu_units_2_io_req_bits_uop_prs3_busy),
    .io_req_bits_uop_stale_pdst(fu_units_2_io_req_bits_uop_stale_pdst),
    .io_req_bits_uop_exception(fu_units_2_io_req_bits_uop_exception),
    .io_req_bits_uop_exc_cause(fu_units_2_io_req_bits_uop_exc_cause),
    .io_req_bits_uop_bypassable(fu_units_2_io_req_bits_uop_bypassable),
    .io_req_bits_uop_mem_cmd(fu_units_2_io_req_bits_uop_mem_cmd),
    .io_req_bits_uop_mem_typ(fu_units_2_io_req_bits_uop_mem_typ),
    .io_req_bits_uop_is_fence(fu_units_2_io_req_bits_uop_is_fence),
    .io_req_bits_uop_is_fencei(fu_units_2_io_req_bits_uop_is_fencei),
    .io_req_bits_uop_is_store(fu_units_2_io_req_bits_uop_is_store),
    .io_req_bits_uop_is_amo(fu_units_2_io_req_bits_uop_is_amo),
    .io_req_bits_uop_is_load(fu_units_2_io_req_bits_uop_is_load),
    .io_req_bits_uop_is_sys_pc2epc(fu_units_2_io_req_bits_uop_is_sys_pc2epc),
    .io_req_bits_uop_is_unique(fu_units_2_io_req_bits_uop_is_unique),
    .io_req_bits_uop_flush_on_commit(fu_units_2_io_req_bits_uop_flush_on_commit),
    .io_req_bits_uop_ldst(fu_units_2_io_req_bits_uop_ldst),
    .io_req_bits_uop_lrs1(fu_units_2_io_req_bits_uop_lrs1),
    .io_req_bits_uop_lrs2(fu_units_2_io_req_bits_uop_lrs2),
    .io_req_bits_uop_lrs3(fu_units_2_io_req_bits_uop_lrs3),
    .io_req_bits_uop_ldst_val(fu_units_2_io_req_bits_uop_ldst_val),
    .io_req_bits_uop_dst_rtype(fu_units_2_io_req_bits_uop_dst_rtype),
    .io_req_bits_uop_lrs1_rtype(fu_units_2_io_req_bits_uop_lrs1_rtype),
    .io_req_bits_uop_lrs2_rtype(fu_units_2_io_req_bits_uop_lrs2_rtype),
    .io_req_bits_uop_frs3_en(fu_units_2_io_req_bits_uop_frs3_en),
    .io_req_bits_uop_fp_val(fu_units_2_io_req_bits_uop_fp_val),
    .io_req_bits_uop_fp_single(fu_units_2_io_req_bits_uop_fp_single),
    .io_req_bits_uop_xcpt_pf_if(fu_units_2_io_req_bits_uop_xcpt_pf_if),
    .io_req_bits_uop_xcpt_ae_if(fu_units_2_io_req_bits_uop_xcpt_ae_if),
    .io_req_bits_uop_replay_if(fu_units_2_io_req_bits_uop_replay_if),
    .io_req_bits_uop_xcpt_ma_if(fu_units_2_io_req_bits_uop_xcpt_ma_if),
    .io_req_bits_uop_debug_wdata(fu_units_2_io_req_bits_uop_debug_wdata),
    .io_req_bits_uop_debug_events_fetch_seq(fu_units_2_io_req_bits_uop_debug_events_fetch_seq),
    .io_req_bits_rs1_data(fu_units_2_io_req_bits_rs1_data),
    .io_req_bits_rs2_data(fu_units_2_io_req_bits_rs2_data),
    .io_req_bits_kill(fu_units_2_io_req_bits_kill),
    .io_resp_ready(fu_units_2_io_resp_ready),
    .io_resp_valid(fu_units_2_io_resp_valid),
    .io_resp_bits_uop_valid(fu_units_2_io_resp_bits_uop_valid),
    .io_resp_bits_uop_iw_state(fu_units_2_io_resp_bits_uop_iw_state),
    .io_resp_bits_uop_uopc(fu_units_2_io_resp_bits_uop_uopc),
    .io_resp_bits_uop_inst(fu_units_2_io_resp_bits_uop_inst),
    .io_resp_bits_uop_pc(fu_units_2_io_resp_bits_uop_pc),
    .io_resp_bits_uop_iqtype(fu_units_2_io_resp_bits_uop_iqtype),
    .io_resp_bits_uop_fu_code(fu_units_2_io_resp_bits_uop_fu_code),
    .io_resp_bits_uop_ctrl_br_type(fu_units_2_io_resp_bits_uop_ctrl_br_type),
    .io_resp_bits_uop_ctrl_op1_sel(fu_units_2_io_resp_bits_uop_ctrl_op1_sel),
    .io_resp_bits_uop_ctrl_op2_sel(fu_units_2_io_resp_bits_uop_ctrl_op2_sel),
    .io_resp_bits_uop_ctrl_imm_sel(fu_units_2_io_resp_bits_uop_ctrl_imm_sel),
    .io_resp_bits_uop_ctrl_op_fcn(fu_units_2_io_resp_bits_uop_ctrl_op_fcn),
    .io_resp_bits_uop_ctrl_fcn_dw(fu_units_2_io_resp_bits_uop_ctrl_fcn_dw),
    .io_resp_bits_uop_ctrl_rf_wen(fu_units_2_io_resp_bits_uop_ctrl_rf_wen),
    .io_resp_bits_uop_ctrl_csr_cmd(fu_units_2_io_resp_bits_uop_ctrl_csr_cmd),
    .io_resp_bits_uop_ctrl_is_load(fu_units_2_io_resp_bits_uop_ctrl_is_load),
    .io_resp_bits_uop_ctrl_is_sta(fu_units_2_io_resp_bits_uop_ctrl_is_sta),
    .io_resp_bits_uop_ctrl_is_std(fu_units_2_io_resp_bits_uop_ctrl_is_std),
    .io_resp_bits_uop_allocate_brtag(fu_units_2_io_resp_bits_uop_allocate_brtag),
    .io_resp_bits_uop_is_br_or_jmp(fu_units_2_io_resp_bits_uop_is_br_or_jmp),
    .io_resp_bits_uop_is_jump(fu_units_2_io_resp_bits_uop_is_jump),
    .io_resp_bits_uop_is_jal(fu_units_2_io_resp_bits_uop_is_jal),
    .io_resp_bits_uop_is_ret(fu_units_2_io_resp_bits_uop_is_ret),
    .io_resp_bits_uop_is_call(fu_units_2_io_resp_bits_uop_is_call),
    .io_resp_bits_uop_br_mask(fu_units_2_io_resp_bits_uop_br_mask),
    .io_resp_bits_uop_br_tag(fu_units_2_io_resp_bits_uop_br_tag),
    .io_resp_bits_uop_br_prediction_btb_blame(fu_units_2_io_resp_bits_uop_br_prediction_btb_blame),
    .io_resp_bits_uop_br_prediction_btb_hit(fu_units_2_io_resp_bits_uop_br_prediction_btb_hit),
    .io_resp_bits_uop_br_prediction_btb_taken(fu_units_2_io_resp_bits_uop_br_prediction_btb_taken),
    .io_resp_bits_uop_br_prediction_bpd_blame(fu_units_2_io_resp_bits_uop_br_prediction_bpd_blame),
    .io_resp_bits_uop_br_prediction_bpd_hit(fu_units_2_io_resp_bits_uop_br_prediction_bpd_hit),
    .io_resp_bits_uop_br_prediction_bpd_taken(fu_units_2_io_resp_bits_uop_br_prediction_bpd_taken),
    .io_resp_bits_uop_br_prediction_bim_resp_value(fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_value),
    .io_resp_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_resp_bits_uop_br_prediction_bim_resp_way_idx(fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_way_idx),
    .io_resp_bits_uop_br_prediction_bpd_resp_takens(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_takens),
    .io_resp_bits_uop_br_prediction_bpd_resp_history(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_u(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_u),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_resp_bits_uop_br_prediction_bpd_resp_info(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_info),
    .io_resp_bits_uop_stat_brjmp_mispredicted(fu_units_2_io_resp_bits_uop_stat_brjmp_mispredicted),
    .io_resp_bits_uop_stat_btb_made_pred(fu_units_2_io_resp_bits_uop_stat_btb_made_pred),
    .io_resp_bits_uop_stat_btb_mispredicted(fu_units_2_io_resp_bits_uop_stat_btb_mispredicted),
    .io_resp_bits_uop_stat_bpd_made_pred(fu_units_2_io_resp_bits_uop_stat_bpd_made_pred),
    .io_resp_bits_uop_stat_bpd_mispredicted(fu_units_2_io_resp_bits_uop_stat_bpd_mispredicted),
    .io_resp_bits_uop_fetch_pc_lob(fu_units_2_io_resp_bits_uop_fetch_pc_lob),
    .io_resp_bits_uop_imm_packed(fu_units_2_io_resp_bits_uop_imm_packed),
    .io_resp_bits_uop_csr_addr(fu_units_2_io_resp_bits_uop_csr_addr),
    .io_resp_bits_uop_rob_idx(fu_units_2_io_resp_bits_uop_rob_idx),
    .io_resp_bits_uop_ldq_idx(fu_units_2_io_resp_bits_uop_ldq_idx),
    .io_resp_bits_uop_stq_idx(fu_units_2_io_resp_bits_uop_stq_idx),
    .io_resp_bits_uop_brob_idx(fu_units_2_io_resp_bits_uop_brob_idx),
    .io_resp_bits_uop_pdst(fu_units_2_io_resp_bits_uop_pdst),
    .io_resp_bits_uop_pop1(fu_units_2_io_resp_bits_uop_pop1),
    .io_resp_bits_uop_pop2(fu_units_2_io_resp_bits_uop_pop2),
    .io_resp_bits_uop_pop3(fu_units_2_io_resp_bits_uop_pop3),
    .io_resp_bits_uop_prs1_busy(fu_units_2_io_resp_bits_uop_prs1_busy),
    .io_resp_bits_uop_prs2_busy(fu_units_2_io_resp_bits_uop_prs2_busy),
    .io_resp_bits_uop_prs3_busy(fu_units_2_io_resp_bits_uop_prs3_busy),
    .io_resp_bits_uop_stale_pdst(fu_units_2_io_resp_bits_uop_stale_pdst),
    .io_resp_bits_uop_exception(fu_units_2_io_resp_bits_uop_exception),
    .io_resp_bits_uop_exc_cause(fu_units_2_io_resp_bits_uop_exc_cause),
    .io_resp_bits_uop_bypassable(fu_units_2_io_resp_bits_uop_bypassable),
    .io_resp_bits_uop_mem_cmd(fu_units_2_io_resp_bits_uop_mem_cmd),
    .io_resp_bits_uop_mem_typ(fu_units_2_io_resp_bits_uop_mem_typ),
    .io_resp_bits_uop_is_fence(fu_units_2_io_resp_bits_uop_is_fence),
    .io_resp_bits_uop_is_fencei(fu_units_2_io_resp_bits_uop_is_fencei),
    .io_resp_bits_uop_is_store(fu_units_2_io_resp_bits_uop_is_store),
    .io_resp_bits_uop_is_amo(fu_units_2_io_resp_bits_uop_is_amo),
    .io_resp_bits_uop_is_load(fu_units_2_io_resp_bits_uop_is_load),
    .io_resp_bits_uop_is_sys_pc2epc(fu_units_2_io_resp_bits_uop_is_sys_pc2epc),
    .io_resp_bits_uop_is_unique(fu_units_2_io_resp_bits_uop_is_unique),
    .io_resp_bits_uop_flush_on_commit(fu_units_2_io_resp_bits_uop_flush_on_commit),
    .io_resp_bits_uop_ldst(fu_units_2_io_resp_bits_uop_ldst),
    .io_resp_bits_uop_lrs1(fu_units_2_io_resp_bits_uop_lrs1),
    .io_resp_bits_uop_lrs2(fu_units_2_io_resp_bits_uop_lrs2),
    .io_resp_bits_uop_lrs3(fu_units_2_io_resp_bits_uop_lrs3),
    .io_resp_bits_uop_ldst_val(fu_units_2_io_resp_bits_uop_ldst_val),
    .io_resp_bits_uop_dst_rtype(fu_units_2_io_resp_bits_uop_dst_rtype),
    .io_resp_bits_uop_lrs1_rtype(fu_units_2_io_resp_bits_uop_lrs1_rtype),
    .io_resp_bits_uop_lrs2_rtype(fu_units_2_io_resp_bits_uop_lrs2_rtype),
    .io_resp_bits_uop_frs3_en(fu_units_2_io_resp_bits_uop_frs3_en),
    .io_resp_bits_uop_fp_val(fu_units_2_io_resp_bits_uop_fp_val),
    .io_resp_bits_uop_fp_single(fu_units_2_io_resp_bits_uop_fp_single),
    .io_resp_bits_uop_xcpt_pf_if(fu_units_2_io_resp_bits_uop_xcpt_pf_if),
    .io_resp_bits_uop_xcpt_ae_if(fu_units_2_io_resp_bits_uop_xcpt_ae_if),
    .io_resp_bits_uop_replay_if(fu_units_2_io_resp_bits_uop_replay_if),
    .io_resp_bits_uop_xcpt_ma_if(fu_units_2_io_resp_bits_uop_xcpt_ma_if),
    .io_resp_bits_uop_debug_wdata(fu_units_2_io_resp_bits_uop_debug_wdata),
    .io_resp_bits_uop_debug_events_fetch_seq(fu_units_2_io_resp_bits_uop_debug_events_fetch_seq),
    .io_resp_bits_data(fu_units_2_io_resp_bits_data),
    .io_brinfo_valid(fu_units_2_io_brinfo_valid),
    .io_brinfo_mispredict(fu_units_2_io_brinfo_mispredict),
    .io_brinfo_mask(fu_units_2_io_brinfo_mask)
  );
  assign _T_1210 = muldiv_busy == 1'h0;
  assign _T_1221 = _T_1210 ? 10'h10 : 10'h0;
  assign _T_1222 = 10'h9 | _T_1221;
  assign _T_1226 = _T_1222 | 10'h20;
  assign _T_1230 = _T_1226 | 10'h2;
  assign _T_1238 = io_req_bits_uop_fu_code == 10'h1;
  assign _T_1239 = io_req_bits_uop_fu_code == 10'h2;
  assign _T_1240 = _T_1238 | _T_1239;
  assign _T_1241 = io_req_bits_uop_fu_code == 10'h20;
  assign _T_1242 = _T_1240 | _T_1241;
  assign _T_1243 = io_req_valid & _T_1242;
  assign _T_1244 = io_req_bits_uop_fu_code == 10'h8;
  assign _T_1245 = io_req_valid & _T_1244;
  assign _T_1326 = io_req_bits_uop_fu_code == 10'h10;
  assign _T_1333 = io_req_valid & _T_1326;
  assign _T_1334 = alu_io_resp_valid | fu_units_1_io_resp_valid;
  assign _T_1336 = _T_1334 == 1'h0;
  assign _T_1338 = fu_units_2_io_req_ready == 1'h0;
  assign _T_1345 = _T_1338 | _T_1333;
  assign _T_1351 = _T_1334 | fu_units_2_io_resp_valid;
  assign _T_1357 = {alu_io_resp_bits_uop_debug_wdata,alu_io_resp_bits_uop_debug_events_fetch_seq};
  assign _T_1358 = {alu_io_resp_bits_uop_xcpt_ae_if,alu_io_resp_bits_uop_replay_if};
  assign _T_1359 = {_T_1358,alu_io_resp_bits_uop_xcpt_ma_if};
  assign _T_1360 = {_T_1359,_T_1357};
  assign _T_1361 = {alu_io_resp_bits_uop_fp_val,alu_io_resp_bits_uop_fp_single};
  assign _T_1362 = {_T_1361,alu_io_resp_bits_uop_xcpt_pf_if};
  assign _T_1363 = {alu_io_resp_bits_uop_lrs1_rtype,alu_io_resp_bits_uop_lrs2_rtype};
  assign _T_1364 = {_T_1363,alu_io_resp_bits_uop_frs3_en};
  assign _T_1365 = {_T_1364,_T_1362};
  assign _T_1366 = {_T_1365,_T_1360};
  assign _T_1367 = {alu_io_resp_bits_uop_ldst_val,alu_io_resp_bits_uop_dst_rtype};
  assign _T_1368 = {alu_io_resp_bits_uop_lrs1,alu_io_resp_bits_uop_lrs2};
  assign _T_1369 = {_T_1368,alu_io_resp_bits_uop_lrs3};
  assign _T_1370 = {_T_1369,_T_1367};
  assign _T_1371 = {alu_io_resp_bits_uop_is_unique,alu_io_resp_bits_uop_flush_on_commit};
  assign _T_1372 = {_T_1371,alu_io_resp_bits_uop_ldst};
  assign _T_1373 = {alu_io_resp_bits_uop_is_amo,alu_io_resp_bits_uop_is_load};
  assign _T_1374 = {_T_1373,alu_io_resp_bits_uop_is_sys_pc2epc};
  assign _T_1375 = {_T_1374,_T_1372};
  assign _T_1376 = {_T_1375,_T_1370};
  assign _T_1377 = {_T_1376,_T_1366};
  assign _T_1378 = {alu_io_resp_bits_uop_is_fencei,alu_io_resp_bits_uop_is_store};
  assign _T_1379 = {alu_io_resp_bits_uop_mem_cmd,alu_io_resp_bits_uop_mem_typ};
  assign _T_1380 = {_T_1379,alu_io_resp_bits_uop_is_fence};
  assign _T_1381 = {_T_1380,_T_1378};
  assign _T_1382 = {alu_io_resp_bits_uop_exception,alu_io_resp_bits_uop_exc_cause};
  assign _T_1383 = {_T_1382,alu_io_resp_bits_uop_bypassable};
  assign _T_1384 = {alu_io_resp_bits_uop_prs2_busy,alu_io_resp_bits_uop_prs3_busy};
  assign _T_1385 = {_T_1384,alu_io_resp_bits_uop_stale_pdst};
  assign _T_1386 = {_T_1385,_T_1383};
  assign _T_1387 = {_T_1386,_T_1381};
  assign _T_1388 = {alu_io_resp_bits_uop_pop2,alu_io_resp_bits_uop_pop3};
  assign _T_1389 = {_T_1388,alu_io_resp_bits_uop_prs1_busy};
  assign _T_1390 = {alu_io_resp_bits_uop_brob_idx,alu_io_resp_bits_uop_pdst};
  assign _T_1391 = {_T_1390,alu_io_resp_bits_uop_pop1};
  assign _T_1392 = {_T_1391,_T_1389};
  assign _T_1393 = {alu_io_resp_bits_uop_rob_idx,alu_io_resp_bits_uop_ldq_idx};
  assign _T_1394 = {_T_1393,alu_io_resp_bits_uop_stq_idx};
  assign _T_1395 = {alu_io_resp_bits_uop_fetch_pc_lob,alu_io_resp_bits_uop_imm_packed};
  assign _T_1396 = {_T_1395,alu_io_resp_bits_uop_csr_addr};
  assign _T_1397 = {_T_1396,_T_1394};
  assign _T_1398 = {_T_1397,_T_1392};
  assign _T_1399 = {_T_1398,_T_1387};
  assign _T_1400 = {_T_1399,_T_1377};
  assign _T_1401 = {alu_io_resp_bits_uop_stat_bpd_made_pred,alu_io_resp_bits_uop_stat_bpd_mispredicted};
  assign _T_1402 = {alu_io_resp_bits_uop_stat_brjmp_mispredicted,alu_io_resp_bits_uop_stat_btb_made_pred};
  assign _T_1403 = {_T_1402,alu_io_resp_bits_uop_stat_btb_mispredicted};
  assign _T_1404 = {_T_1403,_T_1401};
  assign _T_1405 = {alu_io_resp_bits_uop_br_prediction_bpd_resp_history_u,alu_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr};
  assign _T_1406 = {_T_1405,alu_io_resp_bits_uop_br_prediction_bpd_resp_info};
  assign _T_1407 = {alu_io_resp_bits_uop_br_prediction_bim_resp_way_idx,alu_io_resp_bits_uop_br_prediction_bpd_resp_takens};
  assign _T_1408 = {_T_1407,alu_io_resp_bits_uop_br_prediction_bpd_resp_history};
  assign _T_1409 = {_T_1408,_T_1406};
  assign _T_1410 = {_T_1409,_T_1404};
  assign _T_1411 = {alu_io_resp_bits_uop_br_prediction_bim_resp_value,alu_io_resp_bits_uop_br_prediction_bim_resp_entry_idx};
  assign _T_1412 = {alu_io_resp_bits_uop_br_prediction_bpd_blame,alu_io_resp_bits_uop_br_prediction_bpd_hit};
  assign _T_1413 = {_T_1412,alu_io_resp_bits_uop_br_prediction_bpd_taken};
  assign _T_1414 = {_T_1413,_T_1411};
  assign _T_1415 = {alu_io_resp_bits_uop_br_prediction_btb_blame,alu_io_resp_bits_uop_br_prediction_btb_hit};
  assign _T_1416 = {_T_1415,alu_io_resp_bits_uop_br_prediction_btb_taken};
  assign _T_1417 = {alu_io_resp_bits_uop_is_call,alu_io_resp_bits_uop_br_mask};
  assign _T_1418 = {_T_1417,alu_io_resp_bits_uop_br_tag};
  assign _T_1419 = {_T_1418,_T_1416};
  assign _T_1420 = {_T_1419,_T_1414};
  assign _T_1421 = {_T_1420,_T_1410};
  assign _T_1422 = {alu_io_resp_bits_uop_is_jal,alu_io_resp_bits_uop_is_ret};
  assign _T_1423 = {alu_io_resp_bits_uop_allocate_brtag,alu_io_resp_bits_uop_is_br_or_jmp};
  assign _T_1424 = {_T_1423,alu_io_resp_bits_uop_is_jump};
  assign _T_1425 = {_T_1424,_T_1422};
  assign _T_1426 = {alu_io_resp_bits_uop_ctrl_is_load,alu_io_resp_bits_uop_ctrl_is_sta};
  assign _T_1427 = {_T_1426,alu_io_resp_bits_uop_ctrl_is_std};
  assign _T_1428 = {alu_io_resp_bits_uop_ctrl_fcn_dw,alu_io_resp_bits_uop_ctrl_rf_wen};
  assign _T_1429 = {_T_1428,alu_io_resp_bits_uop_ctrl_csr_cmd};
  assign _T_1430 = {_T_1429,_T_1427};
  assign _T_1431 = {_T_1430,_T_1425};
  assign _T_1432 = {alu_io_resp_bits_uop_ctrl_op2_sel,alu_io_resp_bits_uop_ctrl_imm_sel};
  assign _T_1433 = {_T_1432,alu_io_resp_bits_uop_ctrl_op_fcn};
  assign _T_1434 = {alu_io_resp_bits_uop_fu_code,alu_io_resp_bits_uop_ctrl_br_type};
  assign _T_1435 = {_T_1434,alu_io_resp_bits_uop_ctrl_op1_sel};
  assign _T_1436 = {_T_1435,_T_1433};
  assign _T_1437 = {alu_io_resp_bits_uop_inst,alu_io_resp_bits_uop_pc};
  assign _T_1438 = {_T_1437,alu_io_resp_bits_uop_iqtype};
  assign _T_1439 = {alu_io_resp_bits_uop_valid,alu_io_resp_bits_uop_iw_state};
  assign _T_1440 = {_T_1439,alu_io_resp_bits_uop_uopc};
  assign _T_1441 = {_T_1440,_T_1438};
  assign _T_1442 = {_T_1441,_T_1436};
  assign _T_1443 = {_T_1442,_T_1431};
  assign _T_1444 = {_T_1443,_T_1421};
  assign _T_1445 = {_T_1444,_T_1400};
  assign _T_1446 = {fu_units_1_io_resp_bits_uop_debug_wdata,fu_units_1_io_resp_bits_uop_debug_events_fetch_seq};
  assign _T_1447 = {fu_units_1_io_resp_bits_uop_xcpt_ae_if,fu_units_1_io_resp_bits_uop_replay_if};
  assign _T_1448 = {_T_1447,fu_units_1_io_resp_bits_uop_xcpt_ma_if};
  assign _T_1449 = {_T_1448,_T_1446};
  assign _T_1450 = {fu_units_1_io_resp_bits_uop_fp_val,fu_units_1_io_resp_bits_uop_fp_single};
  assign _T_1451 = {_T_1450,fu_units_1_io_resp_bits_uop_xcpt_pf_if};
  assign _T_1452 = {fu_units_1_io_resp_bits_uop_lrs1_rtype,fu_units_1_io_resp_bits_uop_lrs2_rtype};
  assign _T_1453 = {_T_1452,fu_units_1_io_resp_bits_uop_frs3_en};
  assign _T_1454 = {_T_1453,_T_1451};
  assign _T_1455 = {_T_1454,_T_1449};
  assign _T_1456 = {fu_units_1_io_resp_bits_uop_ldst_val,fu_units_1_io_resp_bits_uop_dst_rtype};
  assign _T_1457 = {fu_units_1_io_resp_bits_uop_lrs1,fu_units_1_io_resp_bits_uop_lrs2};
  assign _T_1458 = {_T_1457,fu_units_1_io_resp_bits_uop_lrs3};
  assign _T_1459 = {_T_1458,_T_1456};
  assign _T_1460 = {fu_units_1_io_resp_bits_uop_is_unique,fu_units_1_io_resp_bits_uop_flush_on_commit};
  assign _T_1461 = {_T_1460,fu_units_1_io_resp_bits_uop_ldst};
  assign _T_1462 = {fu_units_1_io_resp_bits_uop_is_amo,fu_units_1_io_resp_bits_uop_is_load};
  assign _T_1463 = {_T_1462,fu_units_1_io_resp_bits_uop_is_sys_pc2epc};
  assign _T_1464 = {_T_1463,_T_1461};
  assign _T_1465 = {_T_1464,_T_1459};
  assign _T_1466 = {_T_1465,_T_1455};
  assign _T_1467 = {fu_units_1_io_resp_bits_uop_is_fencei,fu_units_1_io_resp_bits_uop_is_store};
  assign _T_1468 = {fu_units_1_io_resp_bits_uop_mem_cmd,fu_units_1_io_resp_bits_uop_mem_typ};
  assign _T_1469 = {_T_1468,fu_units_1_io_resp_bits_uop_is_fence};
  assign _T_1470 = {_T_1469,_T_1467};
  assign _T_1471 = {fu_units_1_io_resp_bits_uop_exception,fu_units_1_io_resp_bits_uop_exc_cause};
  assign _T_1472 = {_T_1471,fu_units_1_io_resp_bits_uop_bypassable};
  assign _T_1473 = {fu_units_1_io_resp_bits_uop_prs2_busy,fu_units_1_io_resp_bits_uop_prs3_busy};
  assign _T_1474 = {_T_1473,fu_units_1_io_resp_bits_uop_stale_pdst};
  assign _T_1475 = {_T_1474,_T_1472};
  assign _T_1476 = {_T_1475,_T_1470};
  assign _T_1477 = {fu_units_1_io_resp_bits_uop_pop2,fu_units_1_io_resp_bits_uop_pop3};
  assign _T_1478 = {_T_1477,fu_units_1_io_resp_bits_uop_prs1_busy};
  assign _T_1479 = {fu_units_1_io_resp_bits_uop_brob_idx,fu_units_1_io_resp_bits_uop_pdst};
  assign _T_1480 = {_T_1479,fu_units_1_io_resp_bits_uop_pop1};
  assign _T_1481 = {_T_1480,_T_1478};
  assign _T_1482 = {fu_units_1_io_resp_bits_uop_rob_idx,fu_units_1_io_resp_bits_uop_ldq_idx};
  assign _T_1483 = {_T_1482,fu_units_1_io_resp_bits_uop_stq_idx};
  assign _T_1484 = {fu_units_1_io_resp_bits_uop_fetch_pc_lob,fu_units_1_io_resp_bits_uop_imm_packed};
  assign _T_1485 = {_T_1484,fu_units_1_io_resp_bits_uop_csr_addr};
  assign _T_1486 = {_T_1485,_T_1483};
  assign _T_1487 = {_T_1486,_T_1481};
  assign _T_1488 = {_T_1487,_T_1476};
  assign _T_1489 = {_T_1488,_T_1466};
  assign _T_1490 = {fu_units_1_io_resp_bits_uop_stat_bpd_made_pred,fu_units_1_io_resp_bits_uop_stat_bpd_mispredicted};
  assign _T_1491 = {fu_units_1_io_resp_bits_uop_stat_brjmp_mispredicted,fu_units_1_io_resp_bits_uop_stat_btb_made_pred};
  assign _T_1492 = {_T_1491,fu_units_1_io_resp_bits_uop_stat_btb_mispredicted};
  assign _T_1493 = {_T_1492,_T_1490};
  assign _T_1494 = {fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_u,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr};
  assign _T_1495 = {_T_1494,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_info};
  assign _T_1496 = {fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_way_idx,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_takens};
  assign _T_1497 = {_T_1496,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history};
  assign _T_1498 = {_T_1497,_T_1495};
  assign _T_1499 = {_T_1498,_T_1493};
  assign _T_1500 = {fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_value,fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_entry_idx};
  assign _T_1501 = {fu_units_1_io_resp_bits_uop_br_prediction_bpd_blame,fu_units_1_io_resp_bits_uop_br_prediction_bpd_hit};
  assign _T_1502 = {_T_1501,fu_units_1_io_resp_bits_uop_br_prediction_bpd_taken};
  assign _T_1503 = {_T_1502,_T_1500};
  assign _T_1504 = {fu_units_1_io_resp_bits_uop_br_prediction_btb_blame,fu_units_1_io_resp_bits_uop_br_prediction_btb_hit};
  assign _T_1505 = {_T_1504,fu_units_1_io_resp_bits_uop_br_prediction_btb_taken};
  assign _T_1506 = {fu_units_1_io_resp_bits_uop_is_call,fu_units_1_io_resp_bits_uop_br_mask};
  assign _T_1507 = {_T_1506,fu_units_1_io_resp_bits_uop_br_tag};
  assign _T_1508 = {_T_1507,_T_1505};
  assign _T_1509 = {_T_1508,_T_1503};
  assign _T_1510 = {_T_1509,_T_1499};
  assign _T_1511 = {fu_units_1_io_resp_bits_uop_is_jal,fu_units_1_io_resp_bits_uop_is_ret};
  assign _T_1512 = {fu_units_1_io_resp_bits_uop_allocate_brtag,fu_units_1_io_resp_bits_uop_is_br_or_jmp};
  assign _T_1513 = {_T_1512,fu_units_1_io_resp_bits_uop_is_jump};
  assign _T_1514 = {_T_1513,_T_1511};
  assign _T_1515 = {fu_units_1_io_resp_bits_uop_ctrl_is_load,fu_units_1_io_resp_bits_uop_ctrl_is_sta};
  assign _T_1516 = {_T_1515,fu_units_1_io_resp_bits_uop_ctrl_is_std};
  assign _T_1517 = {fu_units_1_io_resp_bits_uop_ctrl_fcn_dw,fu_units_1_io_resp_bits_uop_ctrl_rf_wen};
  assign _T_1518 = {_T_1517,fu_units_1_io_resp_bits_uop_ctrl_csr_cmd};
  assign _T_1519 = {_T_1518,_T_1516};
  assign _T_1520 = {_T_1519,_T_1514};
  assign _T_1521 = {fu_units_1_io_resp_bits_uop_ctrl_op2_sel,fu_units_1_io_resp_bits_uop_ctrl_imm_sel};
  assign _T_1522 = {_T_1521,fu_units_1_io_resp_bits_uop_ctrl_op_fcn};
  assign _T_1523 = {fu_units_1_io_resp_bits_uop_fu_code,fu_units_1_io_resp_bits_uop_ctrl_br_type};
  assign _T_1524 = {_T_1523,fu_units_1_io_resp_bits_uop_ctrl_op1_sel};
  assign _T_1525 = {_T_1524,_T_1522};
  assign _T_1526 = {fu_units_1_io_resp_bits_uop_inst,fu_units_1_io_resp_bits_uop_pc};
  assign _T_1527 = {_T_1526,fu_units_1_io_resp_bits_uop_iqtype};
  assign _T_1528 = {fu_units_1_io_resp_bits_uop_valid,fu_units_1_io_resp_bits_uop_iw_state};
  assign _T_1529 = {_T_1528,fu_units_1_io_resp_bits_uop_uopc};
  assign _T_1530 = {_T_1529,_T_1527};
  assign _T_1531 = {_T_1530,_T_1525};
  assign _T_1532 = {_T_1531,_T_1520};
  assign _T_1533 = {_T_1532,_T_1510};
  assign _T_1534 = {_T_1533,_T_1489};
  assign _T_1535 = {fu_units_2_io_resp_bits_uop_debug_wdata,fu_units_2_io_resp_bits_uop_debug_events_fetch_seq};
  assign _T_1536 = {fu_units_2_io_resp_bits_uop_xcpt_ae_if,fu_units_2_io_resp_bits_uop_replay_if};
  assign _T_1537 = {_T_1536,fu_units_2_io_resp_bits_uop_xcpt_ma_if};
  assign _T_1538 = {_T_1537,_T_1535};
  assign _T_1539 = {fu_units_2_io_resp_bits_uop_fp_val,fu_units_2_io_resp_bits_uop_fp_single};
  assign _T_1540 = {_T_1539,fu_units_2_io_resp_bits_uop_xcpt_pf_if};
  assign _T_1541 = {fu_units_2_io_resp_bits_uop_lrs1_rtype,fu_units_2_io_resp_bits_uop_lrs2_rtype};
  assign _T_1542 = {_T_1541,fu_units_2_io_resp_bits_uop_frs3_en};
  assign _T_1543 = {_T_1542,_T_1540};
  assign _T_1544 = {_T_1543,_T_1538};
  assign _T_1545 = {fu_units_2_io_resp_bits_uop_ldst_val,fu_units_2_io_resp_bits_uop_dst_rtype};
  assign _T_1546 = {fu_units_2_io_resp_bits_uop_lrs1,fu_units_2_io_resp_bits_uop_lrs2};
  assign _T_1547 = {_T_1546,fu_units_2_io_resp_bits_uop_lrs3};
  assign _T_1548 = {_T_1547,_T_1545};
  assign _T_1549 = {fu_units_2_io_resp_bits_uop_is_unique,fu_units_2_io_resp_bits_uop_flush_on_commit};
  assign _T_1550 = {_T_1549,fu_units_2_io_resp_bits_uop_ldst};
  assign _T_1551 = {fu_units_2_io_resp_bits_uop_is_amo,fu_units_2_io_resp_bits_uop_is_load};
  assign _T_1552 = {_T_1551,fu_units_2_io_resp_bits_uop_is_sys_pc2epc};
  assign _T_1553 = {_T_1552,_T_1550};
  assign _T_1554 = {_T_1553,_T_1548};
  assign _T_1555 = {_T_1554,_T_1544};
  assign _T_1556 = {fu_units_2_io_resp_bits_uop_is_fencei,fu_units_2_io_resp_bits_uop_is_store};
  assign _T_1557 = {fu_units_2_io_resp_bits_uop_mem_cmd,fu_units_2_io_resp_bits_uop_mem_typ};
  assign _T_1558 = {_T_1557,fu_units_2_io_resp_bits_uop_is_fence};
  assign _T_1559 = {_T_1558,_T_1556};
  assign _T_1560 = {fu_units_2_io_resp_bits_uop_exception,fu_units_2_io_resp_bits_uop_exc_cause};
  assign _T_1561 = {_T_1560,fu_units_2_io_resp_bits_uop_bypassable};
  assign _T_1562 = {fu_units_2_io_resp_bits_uop_prs2_busy,fu_units_2_io_resp_bits_uop_prs3_busy};
  assign _T_1563 = {_T_1562,fu_units_2_io_resp_bits_uop_stale_pdst};
  assign _T_1564 = {_T_1563,_T_1561};
  assign _T_1565 = {_T_1564,_T_1559};
  assign _T_1566 = {fu_units_2_io_resp_bits_uop_pop2,fu_units_2_io_resp_bits_uop_pop3};
  assign _T_1567 = {_T_1566,fu_units_2_io_resp_bits_uop_prs1_busy};
  assign _T_1568 = {fu_units_2_io_resp_bits_uop_brob_idx,fu_units_2_io_resp_bits_uop_pdst};
  assign _T_1569 = {_T_1568,fu_units_2_io_resp_bits_uop_pop1};
  assign _T_1570 = {_T_1569,_T_1567};
  assign _T_1571 = {fu_units_2_io_resp_bits_uop_rob_idx,fu_units_2_io_resp_bits_uop_ldq_idx};
  assign _T_1572 = {_T_1571,fu_units_2_io_resp_bits_uop_stq_idx};
  assign _T_1573 = {fu_units_2_io_resp_bits_uop_fetch_pc_lob,fu_units_2_io_resp_bits_uop_imm_packed};
  assign _T_1574 = {_T_1573,fu_units_2_io_resp_bits_uop_csr_addr};
  assign _T_1575 = {_T_1574,_T_1572};
  assign _T_1576 = {_T_1575,_T_1570};
  assign _T_1577 = {_T_1576,_T_1565};
  assign _T_1578 = {_T_1577,_T_1555};
  assign _T_1579 = {fu_units_2_io_resp_bits_uop_stat_bpd_made_pred,fu_units_2_io_resp_bits_uop_stat_bpd_mispredicted};
  assign _T_1580 = {fu_units_2_io_resp_bits_uop_stat_brjmp_mispredicted,fu_units_2_io_resp_bits_uop_stat_btb_made_pred};
  assign _T_1581 = {_T_1580,fu_units_2_io_resp_bits_uop_stat_btb_mispredicted};
  assign _T_1582 = {_T_1581,_T_1579};
  assign _T_1583 = {fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_u,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr};
  assign _T_1584 = {_T_1583,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_info};
  assign _T_1585 = {fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_way_idx,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_takens};
  assign _T_1586 = {_T_1585,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history};
  assign _T_1587 = {_T_1586,_T_1584};
  assign _T_1588 = {_T_1587,_T_1582};
  assign _T_1589 = {fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_value,fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_entry_idx};
  assign _T_1590 = {fu_units_2_io_resp_bits_uop_br_prediction_bpd_blame,fu_units_2_io_resp_bits_uop_br_prediction_bpd_hit};
  assign _T_1591 = {_T_1590,fu_units_2_io_resp_bits_uop_br_prediction_bpd_taken};
  assign _T_1592 = {_T_1591,_T_1589};
  assign _T_1593 = {fu_units_2_io_resp_bits_uop_br_prediction_btb_blame,fu_units_2_io_resp_bits_uop_br_prediction_btb_hit};
  assign _T_1594 = {_T_1593,fu_units_2_io_resp_bits_uop_br_prediction_btb_taken};
  assign _T_1595 = {fu_units_2_io_resp_bits_uop_is_call,fu_units_2_io_resp_bits_uop_br_mask};
  assign _T_1596 = {_T_1595,fu_units_2_io_resp_bits_uop_br_tag};
  assign _T_1597 = {_T_1596,_T_1594};
  assign _T_1598 = {_T_1597,_T_1592};
  assign _T_1599 = {_T_1598,_T_1588};
  assign _T_1600 = {fu_units_2_io_resp_bits_uop_is_jal,fu_units_2_io_resp_bits_uop_is_ret};
  assign _T_1601 = {fu_units_2_io_resp_bits_uop_allocate_brtag,fu_units_2_io_resp_bits_uop_is_br_or_jmp};
  assign _T_1602 = {_T_1601,fu_units_2_io_resp_bits_uop_is_jump};
  assign _T_1603 = {_T_1602,_T_1600};
  assign _T_1604 = {fu_units_2_io_resp_bits_uop_ctrl_is_load,fu_units_2_io_resp_bits_uop_ctrl_is_sta};
  assign _T_1605 = {_T_1604,fu_units_2_io_resp_bits_uop_ctrl_is_std};
  assign _T_1606 = {fu_units_2_io_resp_bits_uop_ctrl_fcn_dw,fu_units_2_io_resp_bits_uop_ctrl_rf_wen};
  assign _T_1607 = {_T_1606,fu_units_2_io_resp_bits_uop_ctrl_csr_cmd};
  assign _T_1608 = {_T_1607,_T_1605};
  assign _T_1609 = {_T_1608,_T_1603};
  assign _T_1610 = {fu_units_2_io_resp_bits_uop_ctrl_op2_sel,fu_units_2_io_resp_bits_uop_ctrl_imm_sel};
  assign _T_1611 = {_T_1610,fu_units_2_io_resp_bits_uop_ctrl_op_fcn};
  assign _T_1612 = {fu_units_2_io_resp_bits_uop_fu_code,fu_units_2_io_resp_bits_uop_ctrl_br_type};
  assign _T_1613 = {_T_1612,fu_units_2_io_resp_bits_uop_ctrl_op1_sel};
  assign _T_1614 = {_T_1613,_T_1611};
  assign _T_1615 = {fu_units_2_io_resp_bits_uop_inst,fu_units_2_io_resp_bits_uop_pc};
  assign _T_1616 = {_T_1615,fu_units_2_io_resp_bits_uop_iqtype};
  assign _T_1617 = {fu_units_2_io_resp_bits_uop_valid,fu_units_2_io_resp_bits_uop_iw_state};
  assign _T_1618 = {_T_1617,fu_units_2_io_resp_bits_uop_uopc};
  assign _T_1619 = {_T_1618,_T_1616};
  assign _T_1620 = {_T_1619,_T_1614};
  assign _T_1621 = {_T_1620,_T_1609};
  assign _T_1622 = {_T_1621,_T_1599};
  assign _T_1623 = {_T_1622,_T_1578};
  assign _T_1624 = fu_units_1_io_resp_valid ? _T_1534 : _T_1623;
  assign _T_1625 = alu_io_resp_valid ? _T_1445 : _T_1624;
  assign _T_1649 = _T_1637[108:107];
  assign _T_1659 = _T_1637[138];
  assign _T_1660 = _T_1637[139];
  assign _T_1665 = _T_1637[150];
  assign _T_1675 = _T_1637[253:247];
  assign _T_1679 = _T_1637[274:268];
  assign _T_1714 = _T_1637[409];
  assign _T_1728 = fu_units_1_io_resp_valid ? fu_units_1_io_resp_bits_data : fu_units_2_io_resp_bits_data;
  assign _T_1729 = alu_io_resp_valid ? alu_io_resp_bits_data : _T_1728;
  assign _T_1730 = alu_io_resp_bits_uop_imm_packed[19];
  assign _T_1731 = $signed(_T_1730);
  assign _T_1735 = {11{_T_1731}};
  assign _T_1741 = {8{_T_1731}};
  assign _T_1747 = alu_io_resp_bits_uop_imm_packed[8];
  assign _T_1748 = $signed(_T_1747);
  assign _T_1753 = alu_io_resp_bits_uop_imm_packed[18:14];
  assign _T_1754 = $signed(_T_1753);
  assign _T_1758 = alu_io_resp_bits_uop_imm_packed[13:9];
  assign _T_1759 = $signed(_T_1758);
  assign _T_1768 = $unsigned(_T_1748);
  assign _T_1769 = $unsigned(_T_1759);
  assign _T_1770 = $unsigned(_T_1754);
  assign _T_1771 = {_T_1770,_T_1769};
  assign _T_1772 = {_T_1771,_T_1768};
  assign _T_1773 = $unsigned(_T_1731);
  assign _T_1774 = $unsigned(_T_1741);
  assign _T_1775 = {_T_1774,_T_1773};
  assign _T_1776 = $unsigned(_T_1735);
  assign _T_1778 = {_T_1773,_T_1776};
  assign _T_1779 = {_T_1778,_T_1775};
  assign _T_1780 = {_T_1779,_T_1772};
  assign _T_1781 = $signed(_T_1780);
  assign _T_1782 = $unsigned(_T_1781);
  assign _T_1796 = fu_units_1_io_resp_valid + fu_units_2_io_resp_valid;
  assign _GEN_0 = {{1'd0}, alu_io_resp_valid};
  assign _T_1797 = _GEN_0 + _T_1796;
  assign _T_1799 = _T_1797 <= 3'h1;
  assign _T_1801 = muldiv_resp_val == 1'h0;
  assign _T_1802 = _T_1799 & _T_1801;
  assign _T_1809 = _T_1797 <= 3'h2;
  assign _T_1811 = _T_1809 & muldiv_resp_val;
  assign _T_1812 = _T_1802 | _T_1811;
  assign _T_1821 = _T_1812 | reset;
  assign _T_1823 = _T_1821 == 1'h0;
  assign io_fu_types = _T_1230;
  assign io_resp_0_valid = _T_1351;
  assign io_resp_0_bits_uop_ctrl_rf_wen = _T_1631_ctrl_rf_wen;
  assign io_resp_0_bits_uop_ctrl_csr_cmd = alu_io_resp_bits_uop_ctrl_csr_cmd;
  assign io_resp_0_bits_uop_csr_addr = _T_1782[11:0];
  assign io_resp_0_bits_uop_rob_idx = _T_1631_rob_idx;
  assign io_resp_0_bits_uop_pdst = _T_1631_pdst;
  assign io_resp_0_bits_uop_bypassable = _T_1631_bypassable;
  assign io_resp_0_bits_uop_is_store = _T_1631_is_store;
  assign io_resp_0_bits_uop_is_amo = _T_1631_is_amo;
  assign io_resp_0_bits_uop_dst_rtype = _T_1631_dst_rtype;
  assign io_resp_0_bits_data = _T_1729;
  assign io_bypass_valid_0 = alu_io_bypass_valid_0;
  assign io_bypass_valid_1 = alu_io_bypass_valid_1;
  assign io_bypass_valid_2 = alu_io_bypass_valid_2;
  assign io_bypass_uop_0_ctrl_rf_wen = alu_io_bypass_uop_0_ctrl_rf_wen;
  assign io_bypass_uop_0_pdst = alu_io_bypass_uop_0_pdst;
  assign io_bypass_uop_0_dst_rtype = alu_io_bypass_uop_0_dst_rtype;
  assign io_bypass_uop_1_ctrl_rf_wen = alu_io_bypass_uop_1_ctrl_rf_wen;
  assign io_bypass_uop_1_pdst = alu_io_bypass_uop_1_pdst;
  assign io_bypass_uop_1_dst_rtype = alu_io_bypass_uop_1_dst_rtype;
  assign io_bypass_uop_2_ctrl_rf_wen = alu_io_bypass_uop_2_ctrl_rf_wen;
  assign io_bypass_uop_2_pdst = alu_io_bypass_uop_2_pdst;
  assign io_bypass_uop_2_dst_rtype = alu_io_bypass_uop_2_dst_rtype;
  assign io_bypass_data_0 = alu_io_bypass_data_0;
  assign io_bypass_data_1 = alu_io_bypass_data_1;
  assign io_bypass_data_2 = alu_io_bypass_data_2;
  assign io_br_unit_take_pc = alu_io_br_unit_take_pc;
  assign io_br_unit_target = alu_io_br_unit_target;
  assign io_br_unit_brinfo_valid = alu_io_br_unit_brinfo_valid;
  assign io_br_unit_brinfo_mispredict = alu_io_br_unit_brinfo_mispredict;
  assign io_br_unit_brinfo_mask = alu_io_br_unit_brinfo_mask;
  assign io_br_unit_brinfo_tag = alu_io_br_unit_brinfo_tag;
  assign io_br_unit_brinfo_exe_mask = alu_io_br_unit_brinfo_exe_mask;
  assign io_br_unit_brinfo_rob_idx = alu_io_br_unit_brinfo_rob_idx;
  assign io_br_unit_brinfo_ldq_idx = alu_io_br_unit_brinfo_ldq_idx;
  assign io_br_unit_brinfo_stq_idx = alu_io_br_unit_brinfo_stq_idx;
  assign io_br_unit_brinfo_is_jr = alu_io_br_unit_brinfo_is_jr;
  assign io_br_unit_btb_update_valid = alu_io_br_unit_btb_update_valid;
  assign io_br_unit_btb_update_bits_pc = alu_io_br_unit_btb_update_bits_pc;
  assign io_br_unit_btb_update_bits_target = alu_io_br_unit_btb_update_bits_target;
  assign io_br_unit_btb_update_bits_cfi_pc = alu_io_br_unit_btb_update_bits_cfi_pc;
  assign io_br_unit_btb_update_bits_bpd_type = alu_io_br_unit_btb_update_bits_bpd_type;
  assign io_br_unit_btb_update_bits_cfi_type = alu_io_br_unit_btb_update_bits_cfi_type;
  assign io_br_unit_bim_update_valid = alu_io_br_unit_bim_update_valid;
  assign io_br_unit_bim_update_bits_taken = alu_io_br_unit_bim_update_bits_taken;
  assign io_br_unit_bim_update_bits_bim_resp_value = alu_io_br_unit_bim_update_bits_bim_resp_value;
  assign io_br_unit_bim_update_bits_bim_resp_entry_idx = alu_io_br_unit_bim_update_bits_bim_resp_entry_idx;
  assign io_br_unit_bim_update_bits_bim_resp_way_idx = alu_io_br_unit_bim_update_bits_bim_resp_way_idx;
  assign io_br_unit_bpd_update_valid = alu_io_br_unit_bpd_update_valid;
  assign io_br_unit_bpd_update_bits_br_pc = alu_io_br_unit_bpd_update_bits_br_pc;
  assign io_br_unit_bpd_update_bits_brob_idx = alu_io_br_unit_bpd_update_bits_brob_idx;
  assign io_br_unit_bpd_update_bits_mispredict = alu_io_br_unit_bpd_update_bits_mispredict;
  assign io_br_unit_bpd_update_bits_history = alu_io_br_unit_bpd_update_bits_history;
  assign io_br_unit_bpd_update_bits_history_ptr = alu_io_br_unit_bpd_update_bits_history_ptr;
  assign io_br_unit_bpd_update_bits_bpd_predict_val = alu_io_br_unit_bpd_update_bits_bpd_predict_val;
  assign io_br_unit_bpd_update_bits_bpd_mispredict = alu_io_br_unit_bpd_update_bits_bpd_mispredict;
  assign io_br_unit_bpd_update_bits_taken = alu_io_br_unit_bpd_update_bits_taken;
  assign io_br_unit_bpd_update_bits_is_br = alu_io_br_unit_bpd_update_bits_is_br;
  assign io_br_unit_bpd_update_bits_new_pc_same_packet = alu_io_br_unit_bpd_update_bits_new_pc_same_packet;
  assign io_br_unit_xcpt_valid = alu_io_br_unit_xcpt_valid;
  assign io_br_unit_xcpt_bits_uop_br_mask = alu_io_br_unit_xcpt_bits_uop_br_mask;
  assign io_br_unit_xcpt_bits_uop_rob_idx = alu_io_br_unit_xcpt_bits_uop_rob_idx;
  assign io_br_unit_xcpt_bits_badvaddr = alu_io_br_unit_xcpt_bits_badvaddr;
  assign muldiv_busy = _T_1345;
  assign alu_io_req_valid = _T_1243;
  assign alu_io_req_bits_uop_valid = io_req_bits_uop_valid;
  assign alu_io_req_bits_uop_iw_state = io_req_bits_uop_iw_state;
  assign alu_io_req_bits_uop_uopc = io_req_bits_uop_uopc;
  assign alu_io_req_bits_uop_inst = io_req_bits_uop_inst;
  assign alu_io_req_bits_uop_pc = io_req_bits_uop_pc;
  assign alu_io_req_bits_uop_iqtype = io_req_bits_uop_iqtype;
  assign alu_io_req_bits_uop_fu_code = io_req_bits_uop_fu_code;
  assign alu_io_req_bits_uop_ctrl_br_type = io_req_bits_uop_ctrl_br_type;
  assign alu_io_req_bits_uop_ctrl_op1_sel = io_req_bits_uop_ctrl_op1_sel;
  assign alu_io_req_bits_uop_ctrl_op2_sel = io_req_bits_uop_ctrl_op2_sel;
  assign alu_io_req_bits_uop_ctrl_imm_sel = io_req_bits_uop_ctrl_imm_sel;
  assign alu_io_req_bits_uop_ctrl_op_fcn = io_req_bits_uop_ctrl_op_fcn;
  assign alu_io_req_bits_uop_ctrl_fcn_dw = io_req_bits_uop_ctrl_fcn_dw;
  assign alu_io_req_bits_uop_ctrl_rf_wen = io_req_bits_uop_ctrl_rf_wen;
  assign alu_io_req_bits_uop_ctrl_csr_cmd = io_req_bits_uop_ctrl_csr_cmd;
  assign alu_io_req_bits_uop_ctrl_is_load = io_req_bits_uop_ctrl_is_load;
  assign alu_io_req_bits_uop_ctrl_is_sta = io_req_bits_uop_ctrl_is_sta;
  assign alu_io_req_bits_uop_ctrl_is_std = io_req_bits_uop_ctrl_is_std;
  assign alu_io_req_bits_uop_allocate_brtag = io_req_bits_uop_allocate_brtag;
  assign alu_io_req_bits_uop_is_br_or_jmp = io_req_bits_uop_is_br_or_jmp;
  assign alu_io_req_bits_uop_is_jump = io_req_bits_uop_is_jump;
  assign alu_io_req_bits_uop_is_jal = io_req_bits_uop_is_jal;
  assign alu_io_req_bits_uop_is_ret = io_req_bits_uop_is_ret;
  assign alu_io_req_bits_uop_is_call = io_req_bits_uop_is_call;
  assign alu_io_req_bits_uop_br_mask = io_req_bits_uop_br_mask;
  assign alu_io_req_bits_uop_br_tag = io_req_bits_uop_br_tag;
  assign alu_io_req_bits_uop_br_prediction_btb_blame = io_req_bits_uop_br_prediction_btb_blame;
  assign alu_io_req_bits_uop_br_prediction_btb_hit = io_req_bits_uop_br_prediction_btb_hit;
  assign alu_io_req_bits_uop_br_prediction_btb_taken = io_req_bits_uop_br_prediction_btb_taken;
  assign alu_io_req_bits_uop_br_prediction_bpd_blame = io_req_bits_uop_br_prediction_bpd_blame;
  assign alu_io_req_bits_uop_br_prediction_bpd_hit = io_req_bits_uop_br_prediction_bpd_hit;
  assign alu_io_req_bits_uop_br_prediction_bpd_taken = io_req_bits_uop_br_prediction_bpd_taken;
  assign alu_io_req_bits_uop_br_prediction_bim_resp_value = io_req_bits_uop_br_prediction_bim_resp_value;
  assign alu_io_req_bits_uop_br_prediction_bim_resp_entry_idx = io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  assign alu_io_req_bits_uop_br_prediction_bim_resp_way_idx = io_req_bits_uop_br_prediction_bim_resp_way_idx;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_takens = io_req_bits_uop_br_prediction_bpd_resp_takens;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_history = io_req_bits_uop_br_prediction_bpd_resp_history;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_history_u = io_req_bits_uop_br_prediction_bpd_resp_history_u;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_history_ptr = io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_info = io_req_bits_uop_br_prediction_bpd_resp_info;
  assign alu_io_req_bits_uop_stat_brjmp_mispredicted = io_req_bits_uop_stat_brjmp_mispredicted;
  assign alu_io_req_bits_uop_stat_btb_made_pred = io_req_bits_uop_stat_btb_made_pred;
  assign alu_io_req_bits_uop_stat_btb_mispredicted = io_req_bits_uop_stat_btb_mispredicted;
  assign alu_io_req_bits_uop_stat_bpd_made_pred = io_req_bits_uop_stat_bpd_made_pred;
  assign alu_io_req_bits_uop_stat_bpd_mispredicted = io_req_bits_uop_stat_bpd_mispredicted;
  assign alu_io_req_bits_uop_fetch_pc_lob = io_req_bits_uop_fetch_pc_lob;
  assign alu_io_req_bits_uop_imm_packed = io_req_bits_uop_imm_packed;
  assign alu_io_req_bits_uop_csr_addr = io_req_bits_uop_csr_addr;
  assign alu_io_req_bits_uop_rob_idx = io_req_bits_uop_rob_idx;
  assign alu_io_req_bits_uop_ldq_idx = io_req_bits_uop_ldq_idx;
  assign alu_io_req_bits_uop_stq_idx = io_req_bits_uop_stq_idx;
  assign alu_io_req_bits_uop_brob_idx = io_req_bits_uop_brob_idx;
  assign alu_io_req_bits_uop_pdst = io_req_bits_uop_pdst;
  assign alu_io_req_bits_uop_pop1 = io_req_bits_uop_pop1;
  assign alu_io_req_bits_uop_pop2 = io_req_bits_uop_pop2;
  assign alu_io_req_bits_uop_pop3 = io_req_bits_uop_pop3;
  assign alu_io_req_bits_uop_prs1_busy = io_req_bits_uop_prs1_busy;
  assign alu_io_req_bits_uop_prs2_busy = io_req_bits_uop_prs2_busy;
  assign alu_io_req_bits_uop_prs3_busy = io_req_bits_uop_prs3_busy;
  assign alu_io_req_bits_uop_stale_pdst = io_req_bits_uop_stale_pdst;
  assign alu_io_req_bits_uop_exception = io_req_bits_uop_exception;
  assign alu_io_req_bits_uop_exc_cause = io_req_bits_uop_exc_cause;
  assign alu_io_req_bits_uop_bypassable = io_req_bits_uop_bypassable;
  assign alu_io_req_bits_uop_mem_cmd = io_req_bits_uop_mem_cmd;
  assign alu_io_req_bits_uop_mem_typ = io_req_bits_uop_mem_typ;
  assign alu_io_req_bits_uop_is_fence = io_req_bits_uop_is_fence;
  assign alu_io_req_bits_uop_is_fencei = io_req_bits_uop_is_fencei;
  assign alu_io_req_bits_uop_is_store = io_req_bits_uop_is_store;
  assign alu_io_req_bits_uop_is_amo = io_req_bits_uop_is_amo;
  assign alu_io_req_bits_uop_is_load = io_req_bits_uop_is_load;
  assign alu_io_req_bits_uop_is_sys_pc2epc = io_req_bits_uop_is_sys_pc2epc;
  assign alu_io_req_bits_uop_is_unique = io_req_bits_uop_is_unique;
  assign alu_io_req_bits_uop_flush_on_commit = io_req_bits_uop_flush_on_commit;
  assign alu_io_req_bits_uop_ldst = io_req_bits_uop_ldst;
  assign alu_io_req_bits_uop_lrs1 = io_req_bits_uop_lrs1;
  assign alu_io_req_bits_uop_lrs2 = io_req_bits_uop_lrs2;
  assign alu_io_req_bits_uop_lrs3 = io_req_bits_uop_lrs3;
  assign alu_io_req_bits_uop_ldst_val = io_req_bits_uop_ldst_val;
  assign alu_io_req_bits_uop_dst_rtype = io_req_bits_uop_dst_rtype;
  assign alu_io_req_bits_uop_lrs1_rtype = io_req_bits_uop_lrs1_rtype;
  assign alu_io_req_bits_uop_lrs2_rtype = io_req_bits_uop_lrs2_rtype;
  assign alu_io_req_bits_uop_frs3_en = io_req_bits_uop_frs3_en;
  assign alu_io_req_bits_uop_fp_val = io_req_bits_uop_fp_val;
  assign alu_io_req_bits_uop_fp_single = io_req_bits_uop_fp_single;
  assign alu_io_req_bits_uop_xcpt_pf_if = io_req_bits_uop_xcpt_pf_if;
  assign alu_io_req_bits_uop_xcpt_ae_if = io_req_bits_uop_xcpt_ae_if;
  assign alu_io_req_bits_uop_replay_if = io_req_bits_uop_replay_if;
  assign alu_io_req_bits_uop_xcpt_ma_if = io_req_bits_uop_xcpt_ma_if;
  assign alu_io_req_bits_uop_debug_wdata = io_req_bits_uop_debug_wdata;
  assign alu_io_req_bits_uop_debug_events_fetch_seq = io_req_bits_uop_debug_events_fetch_seq;
  assign alu_io_req_bits_rs1_data = io_req_bits_rs1_data;
  assign alu_io_req_bits_rs2_data = io_req_bits_rs2_data;
  assign alu_io_req_bits_kill = io_req_bits_kill;
  assign alu_io_brinfo_valid = io_brinfo_valid;
  assign alu_io_brinfo_mispredict = io_brinfo_mispredict;
  assign alu_io_brinfo_mask = io_brinfo_mask;
  assign alu_io_get_rob_pc_curr_pc = io_get_rob_pc_curr_pc;
  assign alu_io_get_rob_pc_curr_brob_idx = io_get_rob_pc_curr_brob_idx;
  assign alu_io_get_rob_pc_next_val = io_get_rob_pc_next_val;
  assign alu_io_get_rob_pc_next_pc = io_get_rob_pc_next_pc;
  assign alu_io_get_pred_info_bim_resp_value = io_get_pred_info_bim_resp_value;
  assign alu_io_get_pred_info_bim_resp_entry_idx = io_get_pred_info_bim_resp_entry_idx;
  assign alu_io_get_pred_info_bim_resp_way_idx = io_get_pred_info_bim_resp_way_idx;
  assign alu_io_get_pred_info_bpd_resp_history = io_get_pred_info_bpd_resp_history;
  assign alu_io_get_pred_info_bpd_resp_history_ptr = io_get_pred_info_bpd_resp_history_ptr;
  assign alu_io_status_debug = io_status_debug;
  assign alu_clock = clock;
  assign alu_reset = reset;
  assign fu_units_1_io_req_valid = _T_1245;
  assign fu_units_1_io_req_bits_uop_valid = io_req_bits_uop_valid;
  assign fu_units_1_io_req_bits_uop_iw_state = io_req_bits_uop_iw_state;
  assign fu_units_1_io_req_bits_uop_uopc = io_req_bits_uop_uopc;
  assign fu_units_1_io_req_bits_uop_inst = io_req_bits_uop_inst;
  assign fu_units_1_io_req_bits_uop_pc = io_req_bits_uop_pc;
  assign fu_units_1_io_req_bits_uop_iqtype = io_req_bits_uop_iqtype;
  assign fu_units_1_io_req_bits_uop_fu_code = io_req_bits_uop_fu_code;
  assign fu_units_1_io_req_bits_uop_ctrl_br_type = io_req_bits_uop_ctrl_br_type;
  assign fu_units_1_io_req_bits_uop_ctrl_op1_sel = io_req_bits_uop_ctrl_op1_sel;
  assign fu_units_1_io_req_bits_uop_ctrl_op2_sel = io_req_bits_uop_ctrl_op2_sel;
  assign fu_units_1_io_req_bits_uop_ctrl_imm_sel = io_req_bits_uop_ctrl_imm_sel;
  assign fu_units_1_io_req_bits_uop_ctrl_op_fcn = io_req_bits_uop_ctrl_op_fcn;
  assign fu_units_1_io_req_bits_uop_ctrl_fcn_dw = io_req_bits_uop_ctrl_fcn_dw;
  assign fu_units_1_io_req_bits_uop_ctrl_rf_wen = io_req_bits_uop_ctrl_rf_wen;
  assign fu_units_1_io_req_bits_uop_ctrl_csr_cmd = io_req_bits_uop_ctrl_csr_cmd;
  assign fu_units_1_io_req_bits_uop_ctrl_is_load = io_req_bits_uop_ctrl_is_load;
  assign fu_units_1_io_req_bits_uop_ctrl_is_sta = io_req_bits_uop_ctrl_is_sta;
  assign fu_units_1_io_req_bits_uop_ctrl_is_std = io_req_bits_uop_ctrl_is_std;
  assign fu_units_1_io_req_bits_uop_allocate_brtag = io_req_bits_uop_allocate_brtag;
  assign fu_units_1_io_req_bits_uop_is_br_or_jmp = io_req_bits_uop_is_br_or_jmp;
  assign fu_units_1_io_req_bits_uop_is_jump = io_req_bits_uop_is_jump;
  assign fu_units_1_io_req_bits_uop_is_jal = io_req_bits_uop_is_jal;
  assign fu_units_1_io_req_bits_uop_is_ret = io_req_bits_uop_is_ret;
  assign fu_units_1_io_req_bits_uop_is_call = io_req_bits_uop_is_call;
  assign fu_units_1_io_req_bits_uop_br_mask = io_req_bits_uop_br_mask;
  assign fu_units_1_io_req_bits_uop_br_tag = io_req_bits_uop_br_tag;
  assign fu_units_1_io_req_bits_uop_br_prediction_btb_blame = io_req_bits_uop_br_prediction_btb_blame;
  assign fu_units_1_io_req_bits_uop_br_prediction_btb_hit = io_req_bits_uop_br_prediction_btb_hit;
  assign fu_units_1_io_req_bits_uop_br_prediction_btb_taken = io_req_bits_uop_br_prediction_btb_taken;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_blame = io_req_bits_uop_br_prediction_bpd_blame;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_hit = io_req_bits_uop_br_prediction_bpd_hit;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_taken = io_req_bits_uop_br_prediction_bpd_taken;
  assign fu_units_1_io_req_bits_uop_br_prediction_bim_resp_value = io_req_bits_uop_br_prediction_bim_resp_value;
  assign fu_units_1_io_req_bits_uop_br_prediction_bim_resp_entry_idx = io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  assign fu_units_1_io_req_bits_uop_br_prediction_bim_resp_way_idx = io_req_bits_uop_br_prediction_bim_resp_way_idx;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_takens = io_req_bits_uop_br_prediction_bpd_resp_takens;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history = io_req_bits_uop_br_prediction_bpd_resp_history;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_u = io_req_bits_uop_br_prediction_bpd_resp_history_u;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_ptr = io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_info = io_req_bits_uop_br_prediction_bpd_resp_info;
  assign fu_units_1_io_req_bits_uop_stat_brjmp_mispredicted = io_req_bits_uop_stat_brjmp_mispredicted;
  assign fu_units_1_io_req_bits_uop_stat_btb_made_pred = io_req_bits_uop_stat_btb_made_pred;
  assign fu_units_1_io_req_bits_uop_stat_btb_mispredicted = io_req_bits_uop_stat_btb_mispredicted;
  assign fu_units_1_io_req_bits_uop_stat_bpd_made_pred = io_req_bits_uop_stat_bpd_made_pred;
  assign fu_units_1_io_req_bits_uop_stat_bpd_mispredicted = io_req_bits_uop_stat_bpd_mispredicted;
  assign fu_units_1_io_req_bits_uop_fetch_pc_lob = io_req_bits_uop_fetch_pc_lob;
  assign fu_units_1_io_req_bits_uop_imm_packed = io_req_bits_uop_imm_packed;
  assign fu_units_1_io_req_bits_uop_csr_addr = io_req_bits_uop_csr_addr;
  assign fu_units_1_io_req_bits_uop_rob_idx = io_req_bits_uop_rob_idx;
  assign fu_units_1_io_req_bits_uop_ldq_idx = io_req_bits_uop_ldq_idx;
  assign fu_units_1_io_req_bits_uop_stq_idx = io_req_bits_uop_stq_idx;
  assign fu_units_1_io_req_bits_uop_brob_idx = io_req_bits_uop_brob_idx;
  assign fu_units_1_io_req_bits_uop_pdst = io_req_bits_uop_pdst;
  assign fu_units_1_io_req_bits_uop_pop1 = io_req_bits_uop_pop1;
  assign fu_units_1_io_req_bits_uop_pop2 = io_req_bits_uop_pop2;
  assign fu_units_1_io_req_bits_uop_pop3 = io_req_bits_uop_pop3;
  assign fu_units_1_io_req_bits_uop_prs1_busy = io_req_bits_uop_prs1_busy;
  assign fu_units_1_io_req_bits_uop_prs2_busy = io_req_bits_uop_prs2_busy;
  assign fu_units_1_io_req_bits_uop_prs3_busy = io_req_bits_uop_prs3_busy;
  assign fu_units_1_io_req_bits_uop_stale_pdst = io_req_bits_uop_stale_pdst;
  assign fu_units_1_io_req_bits_uop_exception = io_req_bits_uop_exception;
  assign fu_units_1_io_req_bits_uop_exc_cause = io_req_bits_uop_exc_cause;
  assign fu_units_1_io_req_bits_uop_bypassable = io_req_bits_uop_bypassable;
  assign fu_units_1_io_req_bits_uop_mem_cmd = io_req_bits_uop_mem_cmd;
  assign fu_units_1_io_req_bits_uop_mem_typ = io_req_bits_uop_mem_typ;
  assign fu_units_1_io_req_bits_uop_is_fence = io_req_bits_uop_is_fence;
  assign fu_units_1_io_req_bits_uop_is_fencei = io_req_bits_uop_is_fencei;
  assign fu_units_1_io_req_bits_uop_is_store = io_req_bits_uop_is_store;
  assign fu_units_1_io_req_bits_uop_is_amo = io_req_bits_uop_is_amo;
  assign fu_units_1_io_req_bits_uop_is_load = io_req_bits_uop_is_load;
  assign fu_units_1_io_req_bits_uop_is_sys_pc2epc = io_req_bits_uop_is_sys_pc2epc;
  assign fu_units_1_io_req_bits_uop_is_unique = io_req_bits_uop_is_unique;
  assign fu_units_1_io_req_bits_uop_flush_on_commit = io_req_bits_uop_flush_on_commit;
  assign fu_units_1_io_req_bits_uop_ldst = io_req_bits_uop_ldst;
  assign fu_units_1_io_req_bits_uop_lrs1 = io_req_bits_uop_lrs1;
  assign fu_units_1_io_req_bits_uop_lrs2 = io_req_bits_uop_lrs2;
  assign fu_units_1_io_req_bits_uop_lrs3 = io_req_bits_uop_lrs3;
  assign fu_units_1_io_req_bits_uop_ldst_val = io_req_bits_uop_ldst_val;
  assign fu_units_1_io_req_bits_uop_dst_rtype = io_req_bits_uop_dst_rtype;
  assign fu_units_1_io_req_bits_uop_lrs1_rtype = io_req_bits_uop_lrs1_rtype;
  assign fu_units_1_io_req_bits_uop_lrs2_rtype = io_req_bits_uop_lrs2_rtype;
  assign fu_units_1_io_req_bits_uop_frs3_en = io_req_bits_uop_frs3_en;
  assign fu_units_1_io_req_bits_uop_fp_val = io_req_bits_uop_fp_val;
  assign fu_units_1_io_req_bits_uop_fp_single = io_req_bits_uop_fp_single;
  assign fu_units_1_io_req_bits_uop_xcpt_pf_if = io_req_bits_uop_xcpt_pf_if;
  assign fu_units_1_io_req_bits_uop_xcpt_ae_if = io_req_bits_uop_xcpt_ae_if;
  assign fu_units_1_io_req_bits_uop_replay_if = io_req_bits_uop_replay_if;
  assign fu_units_1_io_req_bits_uop_xcpt_ma_if = io_req_bits_uop_xcpt_ma_if;
  assign fu_units_1_io_req_bits_uop_debug_wdata = io_req_bits_uop_debug_wdata;
  assign fu_units_1_io_req_bits_uop_debug_events_fetch_seq = io_req_bits_uop_debug_events_fetch_seq;
  assign fu_units_1_io_req_bits_rs1_data = io_req_bits_rs1_data;
  assign fu_units_1_io_req_bits_rs2_data = io_req_bits_rs2_data;
  assign fu_units_1_io_req_bits_kill = io_req_bits_kill;
  assign fu_units_1_io_brinfo_valid = io_brinfo_valid;
  assign fu_units_1_io_brinfo_mispredict = io_brinfo_mispredict;
  assign fu_units_1_io_brinfo_mask = io_brinfo_mask;
  assign fu_units_1_clock = clock;
  assign fu_units_1_reset = reset;
  assign muldiv_resp_val = fu_units_2_io_resp_valid;
  assign fu_units_2_io_req_valid = _T_1333;
  assign fu_units_2_io_req_bits_uop_valid = io_req_bits_uop_valid;
  assign fu_units_2_io_req_bits_uop_iw_state = io_req_bits_uop_iw_state;
  assign fu_units_2_io_req_bits_uop_uopc = io_req_bits_uop_uopc;
  assign fu_units_2_io_req_bits_uop_inst = io_req_bits_uop_inst;
  assign fu_units_2_io_req_bits_uop_pc = io_req_bits_uop_pc;
  assign fu_units_2_io_req_bits_uop_iqtype = io_req_bits_uop_iqtype;
  assign fu_units_2_io_req_bits_uop_fu_code = io_req_bits_uop_fu_code;
  assign fu_units_2_io_req_bits_uop_ctrl_br_type = io_req_bits_uop_ctrl_br_type;
  assign fu_units_2_io_req_bits_uop_ctrl_op1_sel = io_req_bits_uop_ctrl_op1_sel;
  assign fu_units_2_io_req_bits_uop_ctrl_op2_sel = io_req_bits_uop_ctrl_op2_sel;
  assign fu_units_2_io_req_bits_uop_ctrl_imm_sel = io_req_bits_uop_ctrl_imm_sel;
  assign fu_units_2_io_req_bits_uop_ctrl_op_fcn = io_req_bits_uop_ctrl_op_fcn;
  assign fu_units_2_io_req_bits_uop_ctrl_fcn_dw = io_req_bits_uop_ctrl_fcn_dw;
  assign fu_units_2_io_req_bits_uop_ctrl_rf_wen = io_req_bits_uop_ctrl_rf_wen;
  assign fu_units_2_io_req_bits_uop_ctrl_csr_cmd = io_req_bits_uop_ctrl_csr_cmd;
  assign fu_units_2_io_req_bits_uop_ctrl_is_load = io_req_bits_uop_ctrl_is_load;
  assign fu_units_2_io_req_bits_uop_ctrl_is_sta = io_req_bits_uop_ctrl_is_sta;
  assign fu_units_2_io_req_bits_uop_ctrl_is_std = io_req_bits_uop_ctrl_is_std;
  assign fu_units_2_io_req_bits_uop_allocate_brtag = io_req_bits_uop_allocate_brtag;
  assign fu_units_2_io_req_bits_uop_is_br_or_jmp = io_req_bits_uop_is_br_or_jmp;
  assign fu_units_2_io_req_bits_uop_is_jump = io_req_bits_uop_is_jump;
  assign fu_units_2_io_req_bits_uop_is_jal = io_req_bits_uop_is_jal;
  assign fu_units_2_io_req_bits_uop_is_ret = io_req_bits_uop_is_ret;
  assign fu_units_2_io_req_bits_uop_is_call = io_req_bits_uop_is_call;
  assign fu_units_2_io_req_bits_uop_br_mask = io_req_bits_uop_br_mask;
  assign fu_units_2_io_req_bits_uop_br_tag = io_req_bits_uop_br_tag;
  assign fu_units_2_io_req_bits_uop_br_prediction_btb_blame = io_req_bits_uop_br_prediction_btb_blame;
  assign fu_units_2_io_req_bits_uop_br_prediction_btb_hit = io_req_bits_uop_br_prediction_btb_hit;
  assign fu_units_2_io_req_bits_uop_br_prediction_btb_taken = io_req_bits_uop_br_prediction_btb_taken;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_blame = io_req_bits_uop_br_prediction_bpd_blame;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_hit = io_req_bits_uop_br_prediction_bpd_hit;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_taken = io_req_bits_uop_br_prediction_bpd_taken;
  assign fu_units_2_io_req_bits_uop_br_prediction_bim_resp_value = io_req_bits_uop_br_prediction_bim_resp_value;
  assign fu_units_2_io_req_bits_uop_br_prediction_bim_resp_entry_idx = io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  assign fu_units_2_io_req_bits_uop_br_prediction_bim_resp_way_idx = io_req_bits_uop_br_prediction_bim_resp_way_idx;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_takens = io_req_bits_uop_br_prediction_bpd_resp_takens;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history = io_req_bits_uop_br_prediction_bpd_resp_history;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_u = io_req_bits_uop_br_prediction_bpd_resp_history_u;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_ptr = io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_info = io_req_bits_uop_br_prediction_bpd_resp_info;
  assign fu_units_2_io_req_bits_uop_stat_brjmp_mispredicted = io_req_bits_uop_stat_brjmp_mispredicted;
  assign fu_units_2_io_req_bits_uop_stat_btb_made_pred = io_req_bits_uop_stat_btb_made_pred;
  assign fu_units_2_io_req_bits_uop_stat_btb_mispredicted = io_req_bits_uop_stat_btb_mispredicted;
  assign fu_units_2_io_req_bits_uop_stat_bpd_made_pred = io_req_bits_uop_stat_bpd_made_pred;
  assign fu_units_2_io_req_bits_uop_stat_bpd_mispredicted = io_req_bits_uop_stat_bpd_mispredicted;
  assign fu_units_2_io_req_bits_uop_fetch_pc_lob = io_req_bits_uop_fetch_pc_lob;
  assign fu_units_2_io_req_bits_uop_imm_packed = io_req_bits_uop_imm_packed;
  assign fu_units_2_io_req_bits_uop_csr_addr = io_req_bits_uop_csr_addr;
  assign fu_units_2_io_req_bits_uop_rob_idx = io_req_bits_uop_rob_idx;
  assign fu_units_2_io_req_bits_uop_ldq_idx = io_req_bits_uop_ldq_idx;
  assign fu_units_2_io_req_bits_uop_stq_idx = io_req_bits_uop_stq_idx;
  assign fu_units_2_io_req_bits_uop_brob_idx = io_req_bits_uop_brob_idx;
  assign fu_units_2_io_req_bits_uop_pdst = io_req_bits_uop_pdst;
  assign fu_units_2_io_req_bits_uop_pop1 = io_req_bits_uop_pop1;
  assign fu_units_2_io_req_bits_uop_pop2 = io_req_bits_uop_pop2;
  assign fu_units_2_io_req_bits_uop_pop3 = io_req_bits_uop_pop3;
  assign fu_units_2_io_req_bits_uop_prs1_busy = io_req_bits_uop_prs1_busy;
  assign fu_units_2_io_req_bits_uop_prs2_busy = io_req_bits_uop_prs2_busy;
  assign fu_units_2_io_req_bits_uop_prs3_busy = io_req_bits_uop_prs3_busy;
  assign fu_units_2_io_req_bits_uop_stale_pdst = io_req_bits_uop_stale_pdst;
  assign fu_units_2_io_req_bits_uop_exception = io_req_bits_uop_exception;
  assign fu_units_2_io_req_bits_uop_exc_cause = io_req_bits_uop_exc_cause;
  assign fu_units_2_io_req_bits_uop_bypassable = io_req_bits_uop_bypassable;
  assign fu_units_2_io_req_bits_uop_mem_cmd = io_req_bits_uop_mem_cmd;
  assign fu_units_2_io_req_bits_uop_mem_typ = io_req_bits_uop_mem_typ;
  assign fu_units_2_io_req_bits_uop_is_fence = io_req_bits_uop_is_fence;
  assign fu_units_2_io_req_bits_uop_is_fencei = io_req_bits_uop_is_fencei;
  assign fu_units_2_io_req_bits_uop_is_store = io_req_bits_uop_is_store;
  assign fu_units_2_io_req_bits_uop_is_amo = io_req_bits_uop_is_amo;
  assign fu_units_2_io_req_bits_uop_is_load = io_req_bits_uop_is_load;
  assign fu_units_2_io_req_bits_uop_is_sys_pc2epc = io_req_bits_uop_is_sys_pc2epc;
  assign fu_units_2_io_req_bits_uop_is_unique = io_req_bits_uop_is_unique;
  assign fu_units_2_io_req_bits_uop_flush_on_commit = io_req_bits_uop_flush_on_commit;
  assign fu_units_2_io_req_bits_uop_ldst = io_req_bits_uop_ldst;
  assign fu_units_2_io_req_bits_uop_lrs1 = io_req_bits_uop_lrs1;
  assign fu_units_2_io_req_bits_uop_lrs2 = io_req_bits_uop_lrs2;
  assign fu_units_2_io_req_bits_uop_lrs3 = io_req_bits_uop_lrs3;
  assign fu_units_2_io_req_bits_uop_ldst_val = io_req_bits_uop_ldst_val;
  assign fu_units_2_io_req_bits_uop_dst_rtype = io_req_bits_uop_dst_rtype;
  assign fu_units_2_io_req_bits_uop_lrs1_rtype = io_req_bits_uop_lrs1_rtype;
  assign fu_units_2_io_req_bits_uop_lrs2_rtype = io_req_bits_uop_lrs2_rtype;
  assign fu_units_2_io_req_bits_uop_frs3_en = io_req_bits_uop_frs3_en;
  assign fu_units_2_io_req_bits_uop_fp_val = io_req_bits_uop_fp_val;
  assign fu_units_2_io_req_bits_uop_fp_single = io_req_bits_uop_fp_single;
  assign fu_units_2_io_req_bits_uop_xcpt_pf_if = io_req_bits_uop_xcpt_pf_if;
  assign fu_units_2_io_req_bits_uop_xcpt_ae_if = io_req_bits_uop_xcpt_ae_if;
  assign fu_units_2_io_req_bits_uop_replay_if = io_req_bits_uop_replay_if;
  assign fu_units_2_io_req_bits_uop_xcpt_ma_if = io_req_bits_uop_xcpt_ma_if;
  assign fu_units_2_io_req_bits_uop_debug_wdata = io_req_bits_uop_debug_wdata;
  assign fu_units_2_io_req_bits_uop_debug_events_fetch_seq = io_req_bits_uop_debug_events_fetch_seq;
  assign fu_units_2_io_req_bits_rs1_data = io_req_bits_rs1_data;
  assign fu_units_2_io_req_bits_rs2_data = io_req_bits_rs2_data;
  assign fu_units_2_io_req_bits_kill = io_req_bits_kill;
  assign fu_units_2_io_resp_ready = _T_1336;
  assign fu_units_2_io_brinfo_valid = io_brinfo_valid;
  assign fu_units_2_io_brinfo_mispredict = io_brinfo_mispredict;
  assign fu_units_2_io_brinfo_mask = io_brinfo_mask;
  assign fu_units_2_clock = clock;
  assign fu_units_2_reset = reset;
  assign _T_1631_ctrl_rf_wen = _T_1714;
  assign _T_1631_rob_idx = _T_1679;
  assign _T_1631_pdst = _T_1675;
  assign _T_1631_bypassable = _T_1665;
  assign _T_1631_is_store = _T_1660;
  assign _T_1631_is_amo = _T_1659;
  assign _T_1631_dst_rtype = _T_1649;
  assign _T_1637 = _T_1625;
  always @(posedge clock) begin
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (1'h0) begin
          $fwrite(32'h80000002,"Assertion failed\n    at execute.scala:310 assert (io.resp(0).ready) // don'yet support back-pressuring this unit.\n");
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (1'h0) begin
          $fatal;
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1823) begin
          $fwrite(32'h80000002,"Assertion failed: Multiple functional units are fighting over the write port.\n    at execute.scala:324 assert ((PopCount(fu_units.map(_.io.resp.valid)) <= UInt(1) && !muldiv_resp_val && !fdiv_resp_val) ||\n");
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1823) begin
          $fatal;
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
  end
endmodule

module local_MulDivUnit( // @[:boom.system.TestHarness.BoomConfig.fir@141732.2]
  input          clock, // @[:boom.system.TestHarness.BoomConfig.fir@141733.4]
  input          reset, // @[:boom.system.TestHarness.BoomConfig.fir@141734.4]
  output         io_req_ready, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_valid, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_valid, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [8:0]   io_req_bits_uop_uopc, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [31:0]  io_req_bits_uop_inst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [39:0]  io_req_bits_uop_pc, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [1:0]   io_req_bits_uop_iqtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [9:0]   io_req_bits_uop_fu_code, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [3:0]   io_req_bits_uop_ctrl_br_type, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [1:0]   io_req_bits_uop_ctrl_op1_sel, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [2:0]   io_req_bits_uop_ctrl_op2_sel, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [2:0]   io_req_bits_uop_ctrl_imm_sel, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [3:0]   io_req_bits_uop_ctrl_op_fcn, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_ctrl_fcn_dw, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_ctrl_rf_wen, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [2:0]   io_req_bits_uop_ctrl_csr_cmd, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_ctrl_is_load, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_ctrl_is_sta, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_ctrl_is_std, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [1:0]   io_req_bits_uop_iw_state, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_allocate_brtag, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_br_or_jmp, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_jump, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_jal, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_ret, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_call, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [7:0]   io_req_bits_uop_br_mask, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [2:0]   io_req_bits_uop_br_tag, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_br_prediction_btb_blame, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_br_prediction_btb_hit, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_br_prediction_btb_taken, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_br_prediction_bpd_blame, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_br_prediction_bpd_hit, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_br_prediction_bpd_taken, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [7:0]   io_req_bits_uop_br_prediction_bim_resp_rowdata, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [9:0]   io_req_bits_uop_br_prediction_bim_resp_entry_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [3:0]   io_req_bits_uop_br_prediction_bpd_resp_takens, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [89:0]  io_req_bits_uop_br_prediction_bpd_resp_history, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [141:0] io_req_bits_uop_br_prediction_bpd_resp_info, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_stat_brjmp_mispredicted, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_stat_btb_made_pred, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_stat_btb_mispredicted, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_stat_bpd_made_pred, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_stat_bpd_mispredicted, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [3:0]   io_req_bits_uop_ftq_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [5:0]   io_req_bits_uop_pc_lob, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [19:0]  io_req_bits_uop_imm_packed, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [11:0]  io_req_bits_uop_csr_addr, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [6:0]   io_req_bits_uop_rob_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [3:0]   io_req_bits_uop_ldq_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [3:0]   io_req_bits_uop_stq_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [6:0]   io_req_bits_uop_pdst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [6:0]   io_req_bits_uop_pop1, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [6:0]   io_req_bits_uop_pop2, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [6:0]   io_req_bits_uop_pop3, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_prs1_busy, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_prs2_busy, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_prs3_busy, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [6:0]   io_req_bits_uop_stale_pdst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_exception, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [63:0]  io_req_bits_uop_exc_cause, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_bypassable, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [4:0]   io_req_bits_uop_mem_cmd, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [2:0]   io_req_bits_uop_mem_typ, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_fence, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_fencei, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_store, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_amo, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_load, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_sys_pc2epc, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_is_unique, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_flush_on_commit, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [5:0]   io_req_bits_uop_ldst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [5:0]   io_req_bits_uop_lrs1, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [5:0]   io_req_bits_uop_lrs2, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [5:0]   io_req_bits_uop_lrs3, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_ldst_val, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [1:0]   io_req_bits_uop_dst_rtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [1:0]   io_req_bits_uop_lrs1_rtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [1:0]   io_req_bits_uop_lrs2_rtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_frs3_en, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_fp_val, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_fp_single, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_xcpt_pf_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_xcpt_ae_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_replay_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_uop_xcpt_ma_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [63:0]  io_req_bits_uop_debug_wdata, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [31:0]  io_req_bits_uop_debug_events_fetch_seq, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [63:0]  io_req_bits_rs1_data, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [63:0]  io_req_bits_rs2_data, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_req_bits_kill, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_resp_ready, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_valid, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_valid, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [8:0]   io_resp_bits_uop_uopc, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [31:0]  io_resp_bits_uop_inst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [39:0]  io_resp_bits_uop_pc, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [1:0]   io_resp_bits_uop_iqtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [9:0]   io_resp_bits_uop_fu_code, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [3:0]   io_resp_bits_uop_ctrl_br_type, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [1:0]   io_resp_bits_uop_ctrl_op1_sel, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [2:0]   io_resp_bits_uop_ctrl_op2_sel, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [2:0]   io_resp_bits_uop_ctrl_imm_sel, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [3:0]   io_resp_bits_uop_ctrl_op_fcn, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_ctrl_fcn_dw, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_ctrl_rf_wen, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [2:0]   io_resp_bits_uop_ctrl_csr_cmd, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_ctrl_is_load, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_ctrl_is_sta, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_ctrl_is_std, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [1:0]   io_resp_bits_uop_iw_state, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_allocate_brtag, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_br_or_jmp, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_jump, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_jal, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_ret, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_call, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [7:0]   io_resp_bits_uop_br_mask, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [2:0]   io_resp_bits_uop_br_tag, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_br_prediction_btb_blame, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_br_prediction_btb_hit, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_br_prediction_btb_taken, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_br_prediction_bpd_blame, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_br_prediction_bpd_hit, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_br_prediction_bpd_taken, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [7:0]   io_resp_bits_uop_br_prediction_bim_resp_rowdata, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [9:0]   io_resp_bits_uop_br_prediction_bim_resp_entry_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [3:0]   io_resp_bits_uop_br_prediction_bpd_resp_takens, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [89:0]  io_resp_bits_uop_br_prediction_bpd_resp_history, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [141:0] io_resp_bits_uop_br_prediction_bpd_resp_info, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_stat_brjmp_mispredicted, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_stat_btb_made_pred, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_stat_btb_mispredicted, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_stat_bpd_made_pred, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_stat_bpd_mispredicted, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [3:0]   io_resp_bits_uop_ftq_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [5:0]   io_resp_bits_uop_pc_lob, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [19:0]  io_resp_bits_uop_imm_packed, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [11:0]  io_resp_bits_uop_csr_addr, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [6:0]   io_resp_bits_uop_rob_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [3:0]   io_resp_bits_uop_ldq_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [3:0]   io_resp_bits_uop_stq_idx, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [6:0]   io_resp_bits_uop_pdst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [6:0]   io_resp_bits_uop_pop1, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [6:0]   io_resp_bits_uop_pop2, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [6:0]   io_resp_bits_uop_pop3, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_prs1_busy, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_prs2_busy, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_prs3_busy, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [6:0]   io_resp_bits_uop_stale_pdst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_exception, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [63:0]  io_resp_bits_uop_exc_cause, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_bypassable, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [4:0]   io_resp_bits_uop_mem_cmd, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [2:0]   io_resp_bits_uop_mem_typ, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_fence, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_fencei, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_store, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_amo, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_load, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_sys_pc2epc, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_is_unique, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_flush_on_commit, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [5:0]   io_resp_bits_uop_ldst, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [5:0]   io_resp_bits_uop_lrs1, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [5:0]   io_resp_bits_uop_lrs2, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [5:0]   io_resp_bits_uop_lrs3, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_ldst_val, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [1:0]   io_resp_bits_uop_dst_rtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [1:0]   io_resp_bits_uop_lrs1_rtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [1:0]   io_resp_bits_uop_lrs2_rtype, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_frs3_en, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_fp_val, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_fp_single, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_xcpt_pf_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_xcpt_ae_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_replay_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output         io_resp_bits_uop_xcpt_ma_if, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [63:0]  io_resp_bits_uop_debug_wdata, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [31:0]  io_resp_bits_uop_debug_events_fetch_seq, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  output [63:0]  io_resp_bits_data, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_brinfo_valid, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input          io_brinfo_mispredict, // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
  input  [7:0]   io_brinfo_mask // @[:boom.system.TestHarness.BoomConfig.fir@141735.4]
);
  wire  muldiv_clock; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire  muldiv_reset; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire  muldiv_io_req_ready; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire  muldiv_io_req_valid; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire [3:0] muldiv_io_req_bits_fn; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire  muldiv_io_req_bits_dw; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire [63:0] muldiv_io_req_bits_in1; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire [63:0] muldiv_io_req_bits_in2; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire  muldiv_io_kill; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire  muldiv_io_resp_ready; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire  muldiv_io_resp_valid; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  wire [63:0] muldiv_io_resp_bits_data; // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
  reg  r_uop_valid; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_0;
  reg [8:0] r_uop_uopc; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_1;
  reg [31:0] r_uop_inst; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_2;
  reg [39:0] r_uop_pc; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [63:0] _RAND_3;
  reg [1:0] r_uop_iqtype; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_4;
  reg [9:0] r_uop_fu_code; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_5;
  reg [3:0] r_uop_ctrl_br_type; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_6;
  reg [1:0] r_uop_ctrl_op1_sel; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_7;
  reg [2:0] r_uop_ctrl_op2_sel; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_8;
  reg [2:0] r_uop_ctrl_imm_sel; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_9;
  reg [3:0] r_uop_ctrl_op_fcn; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_10;
  reg  r_uop_ctrl_fcn_dw; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_11;
  reg  r_uop_ctrl_rf_wen; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_12;
  reg [2:0] r_uop_ctrl_csr_cmd; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_13;
  reg  r_uop_ctrl_is_load; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_14;
  reg  r_uop_ctrl_is_sta; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_15;
  reg  r_uop_ctrl_is_std; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_16;
  reg [1:0] r_uop_iw_state; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_17;
  reg  r_uop_allocate_brtag; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_18;
  reg  r_uop_is_br_or_jmp; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_19;
  reg  r_uop_is_jump; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_20;
  reg  r_uop_is_jal; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_21;
  reg  r_uop_is_ret; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_22;
  reg  r_uop_is_call; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_23;
  reg [7:0] r_uop_br_mask; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_24;
  reg [2:0] r_uop_br_tag; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_25;
  reg  r_uop_br_prediction_btb_blame; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_26;
  reg  r_uop_br_prediction_btb_hit; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_27;
  reg  r_uop_br_prediction_btb_taken; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_28;
  reg  r_uop_br_prediction_bpd_blame; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_29;
  reg  r_uop_br_prediction_bpd_hit; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_30;
  reg  r_uop_br_prediction_bpd_taken; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_31;
  reg [7:0] r_uop_br_prediction_bim_resp_rowdata; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_32;
  reg [9:0] r_uop_br_prediction_bim_resp_entry_idx; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_33;
  reg [3:0] r_uop_br_prediction_bpd_resp_takens; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_34;
  reg [89:0] r_uop_br_prediction_bpd_resp_history; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [95:0] _RAND_35;
  reg [141:0] r_uop_br_prediction_bpd_resp_info; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [159:0] _RAND_36;
  reg  r_uop_stat_brjmp_mispredicted; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_37;
  reg  r_uop_stat_btb_made_pred; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_38;
  reg  r_uop_stat_btb_mispredicted; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_39;
  reg  r_uop_stat_bpd_made_pred; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_40;
  reg  r_uop_stat_bpd_mispredicted; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_41;
  reg [3:0] r_uop_ftq_idx; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_42;
  reg [5:0] r_uop_pc_lob; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_43;
  reg [19:0] r_uop_imm_packed; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_44;
  reg [11:0] r_uop_csr_addr; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_45;
  reg [6:0] r_uop_rob_idx; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_46;
  reg [3:0] r_uop_ldq_idx; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_47;
  reg [3:0] r_uop_stq_idx; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_48;
  reg [6:0] r_uop_pdst; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_49;
  reg [6:0] r_uop_pop1; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_50;
  reg [6:0] r_uop_pop2; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_51;
  reg [6:0] r_uop_pop3; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_52;
  reg  r_uop_prs1_busy; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_53;
  reg  r_uop_prs2_busy; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_54;
  reg  r_uop_prs3_busy; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_55;
  reg [6:0] r_uop_stale_pdst; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_56;
  reg  r_uop_exception; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_57;
  reg [63:0] r_uop_exc_cause; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [63:0] _RAND_58;
  reg  r_uop_bypassable; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_59;
  reg [4:0] r_uop_mem_cmd; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_60;
  reg [2:0] r_uop_mem_typ; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_61;
  reg  r_uop_is_fence; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_62;
  reg  r_uop_is_fencei; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_63;
  reg  r_uop_is_store; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_64;
  reg  r_uop_is_amo; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_65;
  reg  r_uop_is_load; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_66;
  reg  r_uop_is_sys_pc2epc; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_67;
  reg  r_uop_is_unique; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_68;
  reg  r_uop_flush_on_commit; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_69;
  reg [5:0] r_uop_ldst; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_70;
  reg [5:0] r_uop_lrs1; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_71;
  reg [5:0] r_uop_lrs2; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_72;
  reg [5:0] r_uop_lrs3; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_73;
  reg  r_uop_ldst_val; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_74;
  reg [1:0] r_uop_dst_rtype; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_75;
  reg [1:0] r_uop_lrs1_rtype; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_76;
  reg [1:0] r_uop_lrs2_rtype; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_77;
  reg  r_uop_frs3_en; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_78;
  reg  r_uop_fp_val; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_79;
  reg  r_uop_fp_single; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_80;
  reg  r_uop_xcpt_pf_if; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_81;
  reg  r_uop_xcpt_ae_if; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_82;
  reg  r_uop_replay_if; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_83;
  reg  r_uop_xcpt_ma_if; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_84;
  reg [63:0] r_uop_debug_wdata; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [63:0] _RAND_85;
  reg [31:0] r_uop_debug_events_fetch_seq; // @[functional_unit.scala 707:19:boom.system.TestHarness.BoomConfig.fir@141740.4]
  reg [31:0] _RAND_86;
  wire  _T_2730; // @[Decoupled.scala 37:37:boom.system.TestHarness.BoomConfig.fir@141744.4]
  wire  _T_2731; // @[util.scala 51:28:boom.system.TestHarness.BoomConfig.fir@141746.6]
  wire [7:0] _T_2732; // @[util.scala 95:52:boom.system.TestHarness.BoomConfig.fir@141747.6]
  wire  _T_2733; // @[util.scala 95:60:boom.system.TestHarness.BoomConfig.fir@141748.6]
  wire  _T_2734; // @[util.scala 52:33:boom.system.TestHarness.BoomConfig.fir@141749.6]
  wire  _T_2735; // @[functional_unit.scala 715:63:boom.system.TestHarness.BoomConfig.fir@141750.6]
  wire [7:0] _T_2736; // @[util.scala 82:47:boom.system.TestHarness.BoomConfig.fir@141753.6]
  wire [7:0] _T_2737; // @[util.scala 82:45:boom.system.TestHarness.BoomConfig.fir@141754.6]
  wire [7:0] _T_2740; // @[util.scala 95:52:boom.system.TestHarness.BoomConfig.fir@141760.6]
  wire  _T_2741; // @[util.scala 95:60:boom.system.TestHarness.BoomConfig.fir@141761.6]
  wire  _T_2742; // @[util.scala 52:33:boom.system.TestHarness.BoomConfig.fir@141762.6]
  wire  _T_2743; // @[functional_unit.scala 721:53:boom.system.TestHarness.BoomConfig.fir@141763.6]
  wire [7:0] _T_2745; // @[util.scala 82:45:boom.system.TestHarness.BoomConfig.fir@141766.6]
  wire  do_kill; // @[functional_unit.scala 713:4:boom.system.TestHarness.BoomConfig.fir@141745.4]
  wire  _T_2747; // @[functional_unit.scala 736:46:boom.system.TestHarness.BoomConfig.fir@141775.4]
  local_MulDiv muldiv ( // @[functional_unit.scala 733:23:boom.system.TestHarness.BoomConfig.fir@141771.4]
    .clock(muldiv_clock),
    .reset(muldiv_reset),
    .io_req_ready(muldiv_io_req_ready),
    .io_req_valid(muldiv_io_req_valid),
    .io_req_bits_fn(muldiv_io_req_bits_fn),
    .io_req_bits_dw(muldiv_io_req_bits_dw),
    .io_req_bits_in1(muldiv_io_req_bits_in1),
    .io_req_bits_in2(muldiv_io_req_bits_in2),
    .io_kill(muldiv_io_kill),
    .io_resp_ready(muldiv_io_resp_ready),
    .io_resp_valid(muldiv_io_resp_valid),
    .io_resp_bits_data(muldiv_io_resp_bits_data)
  );
  assign _T_2730 = io_req_ready & io_req_valid; // @[Decoupled.scala 37:37:boom.system.TestHarness.BoomConfig.fir@141744.4]
  assign _T_2731 = io_brinfo_valid & io_brinfo_mispredict; // @[util.scala 51:28:boom.system.TestHarness.BoomConfig.fir@141746.6]
  assign _T_2732 = io_brinfo_mask & io_req_bits_uop_br_mask; // @[util.scala 95:52:boom.system.TestHarness.BoomConfig.fir@141747.6]
  assign _T_2733 = _T_2732 != 8'h0; // @[util.scala 95:60:boom.system.TestHarness.BoomConfig.fir@141748.6]
  assign _T_2734 = _T_2731 & _T_2733; // @[util.scala 52:33:boom.system.TestHarness.BoomConfig.fir@141749.6]
  assign _T_2735 = _T_2734 | io_req_bits_kill; // @[functional_unit.scala 715:63:boom.system.TestHarness.BoomConfig.fir@141750.6]
  assign _T_2736 = ~ io_brinfo_mask; // @[util.scala 82:47:boom.system.TestHarness.BoomConfig.fir@141753.6]
  assign _T_2737 = io_req_bits_uop_br_mask & _T_2736; // @[util.scala 82:45:boom.system.TestHarness.BoomConfig.fir@141754.6]
  assign _T_2740 = io_brinfo_mask & r_uop_br_mask; // @[util.scala 95:52:boom.system.TestHarness.BoomConfig.fir@141760.6]
  assign _T_2741 = _T_2740 != 8'h0; // @[util.scala 95:60:boom.system.TestHarness.BoomConfig.fir@141761.6]
  assign _T_2742 = _T_2731 & _T_2741; // @[util.scala 52:33:boom.system.TestHarness.BoomConfig.fir@141762.6]
  assign _T_2743 = _T_2742 | io_req_bits_kill; // @[functional_unit.scala 721:53:boom.system.TestHarness.BoomConfig.fir@141763.6]
  assign _T_2745 = r_uop_br_mask & _T_2736; // @[util.scala 82:45:boom.system.TestHarness.BoomConfig.fir@141766.6]
  assign do_kill = _T_2730 ? _T_2735 : _T_2743; // @[functional_unit.scala 713:4:boom.system.TestHarness.BoomConfig.fir@141745.4]
  assign _T_2747 = do_kill == 1'h0; // @[functional_unit.scala 736:46:boom.system.TestHarness.BoomConfig.fir@141775.4]
  assign io_req_ready = muldiv_io_req_ready; // @[functional_unit.scala 741:27:boom.system.TestHarness.BoomConfig.fir@141782.4]
  assign io_resp_valid = muldiv_io_resp_valid & _T_2747; // @[functional_unit.scala 747:27:boom.system.TestHarness.BoomConfig.fir@141786.4]
  assign io_resp_bits_uop_valid = r_uop_valid; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_uopc = r_uop_uopc; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_inst = r_uop_inst; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_pc = r_uop_pc; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_iqtype = r_uop_iqtype; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_fu_code = r_uop_fu_code; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_br_type = r_uop_ctrl_br_type; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_op1_sel = r_uop_ctrl_op1_sel; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_op2_sel = r_uop_ctrl_op2_sel; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_imm_sel = r_uop_ctrl_imm_sel; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_op_fcn = r_uop_ctrl_op_fcn; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_fcn_dw = r_uop_ctrl_fcn_dw; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_rf_wen = r_uop_ctrl_rf_wen; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_csr_cmd = r_uop_ctrl_csr_cmd; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_is_load = r_uop_ctrl_is_load; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_is_sta = r_uop_ctrl_is_sta; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ctrl_is_std = r_uop_ctrl_is_std; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_iw_state = r_uop_iw_state; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_allocate_brtag = r_uop_allocate_brtag; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_br_or_jmp = r_uop_is_br_or_jmp; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_jump = r_uop_is_jump; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_jal = r_uop_is_jal; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_ret = r_uop_is_ret; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_call = r_uop_is_call; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_mask = r_uop_br_mask; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_tag = r_uop_br_tag; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_btb_blame = r_uop_br_prediction_btb_blame; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_btb_hit = r_uop_br_prediction_btb_hit; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_btb_taken = r_uop_br_prediction_btb_taken; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bpd_blame = r_uop_br_prediction_bpd_blame; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bpd_hit = r_uop_br_prediction_bpd_hit; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bpd_taken = r_uop_br_prediction_bpd_taken; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bim_resp_rowdata = r_uop_br_prediction_bim_resp_rowdata; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bim_resp_entry_idx = r_uop_br_prediction_bim_resp_entry_idx; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bpd_resp_takens = r_uop_br_prediction_bpd_resp_takens; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bpd_resp_history = r_uop_br_prediction_bpd_resp_history; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_br_prediction_bpd_resp_info = r_uop_br_prediction_bpd_resp_info; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_stat_brjmp_mispredicted = r_uop_stat_brjmp_mispredicted; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_stat_btb_made_pred = r_uop_stat_btb_made_pred; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_stat_btb_mispredicted = r_uop_stat_btb_mispredicted; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_stat_bpd_made_pred = r_uop_stat_bpd_made_pred; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_stat_bpd_mispredicted = r_uop_stat_bpd_mispredicted; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ftq_idx = r_uop_ftq_idx; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_pc_lob = r_uop_pc_lob; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_imm_packed = r_uop_imm_packed; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_csr_addr = r_uop_csr_addr; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_rob_idx = r_uop_rob_idx; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ldq_idx = r_uop_ldq_idx; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_stq_idx = r_uop_stq_idx; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_pdst = r_uop_pdst; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_pop1 = r_uop_pop1; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_pop2 = r_uop_pop2; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_pop3 = r_uop_pop3; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_prs1_busy = r_uop_prs1_busy; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_prs2_busy = r_uop_prs2_busy; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_prs3_busy = r_uop_prs3_busy; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_stale_pdst = r_uop_stale_pdst; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_exception = r_uop_exception; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_exc_cause = r_uop_exc_cause; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_bypassable = r_uop_bypassable; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_mem_cmd = r_uop_mem_cmd; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_mem_typ = r_uop_mem_typ; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_fence = r_uop_is_fence; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_fencei = r_uop_is_fencei; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_store = r_uop_is_store; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_amo = r_uop_is_amo; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_load = r_uop_is_load; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_sys_pc2epc = r_uop_is_sys_pc2epc; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_is_unique = r_uop_is_unique; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_flush_on_commit = r_uop_flush_on_commit; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ldst = r_uop_ldst; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_lrs1 = r_uop_lrs1; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_lrs2 = r_uop_lrs2; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_lrs3 = r_uop_lrs3; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_ldst_val = r_uop_ldst_val; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_dst_rtype = r_uop_dst_rtype; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_lrs1_rtype = r_uop_lrs1_rtype; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_lrs2_rtype = r_uop_lrs2_rtype; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_frs3_en = r_uop_frs3_en; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_fp_val = r_uop_fp_val; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_fp_single = r_uop_fp_single; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_xcpt_pf_if = r_uop_xcpt_pf_if; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_xcpt_ae_if = r_uop_xcpt_ae_if; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_replay_if = r_uop_replay_if; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_xcpt_ma_if = r_uop_xcpt_ma_if; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_debug_wdata = r_uop_debug_wdata; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_uop_debug_events_fetch_seq = r_uop_debug_events_fetch_seq; // @[functional_unit.scala 727:21:boom.system.TestHarness.BoomConfig.fir@141770.4]
  assign io_resp_bits_data = muldiv_io_resp_bits_data; // @[functional_unit.scala 749:27:boom.system.TestHarness.BoomConfig.fir@141788.4]
  assign muldiv_clock = clock; // @[:boom.system.TestHarness.BoomConfig.fir@141773.4]
  assign muldiv_reset = reset; // @[:boom.system.TestHarness.BoomConfig.fir@141774.4]
  assign muldiv_io_req_valid = io_req_valid & _T_2747; // @[functional_unit.scala 736:27:boom.system.TestHarness.BoomConfig.fir@141777.4]
  assign muldiv_io_req_bits_fn = io_req_bits_uop_ctrl_op_fcn; // @[functional_unit.scala 738:27:boom.system.TestHarness.BoomConfig.fir@141779.4]
  assign muldiv_io_req_bits_dw = io_req_bits_uop_ctrl_fcn_dw; // @[functional_unit.scala 737:27:boom.system.TestHarness.BoomConfig.fir@141778.4]
  assign muldiv_io_req_bits_in1 = io_req_bits_rs1_data; // @[functional_unit.scala 739:27:boom.system.TestHarness.BoomConfig.fir@141780.4]
  assign muldiv_io_req_bits_in2 = io_req_bits_rs2_data; // @[functional_unit.scala 740:27:boom.system.TestHarness.BoomConfig.fir@141781.4]
  assign muldiv_io_kill = _T_2730 ? _T_2735 : _T_2743; // @[functional_unit.scala 744:27:boom.system.TestHarness.BoomConfig.fir@141783.4]
  assign muldiv_io_resp_ready = io_resp_ready; // @[functional_unit.scala 748:27:boom.system.TestHarness.BoomConfig.fir@141787.4]
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE
  integer initvar;
  initial begin
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
  `ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  r_uop_valid = _RAND_0[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  r_uop_uopc = _RAND_1[8:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_2 = {1{`RANDOM}};
  r_uop_inst = _RAND_2[31:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_3 = {2{`RANDOM}};
  r_uop_pc = _RAND_3[39:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_4 = {1{`RANDOM}};
  r_uop_iqtype = _RAND_4[1:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_5 = {1{`RANDOM}};
  r_uop_fu_code = _RAND_5[9:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_6 = {1{`RANDOM}};
  r_uop_ctrl_br_type = _RAND_6[3:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_7 = {1{`RANDOM}};
  r_uop_ctrl_op1_sel = _RAND_7[1:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_8 = {1{`RANDOM}};
  r_uop_ctrl_op2_sel = _RAND_8[2:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_9 = {1{`RANDOM}};
  r_uop_ctrl_imm_sel = _RAND_9[2:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_10 = {1{`RANDOM}};
  r_uop_ctrl_op_fcn = _RAND_10[3:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_11 = {1{`RANDOM}};
  r_uop_ctrl_fcn_dw = _RAND_11[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_12 = {1{`RANDOM}};
  r_uop_ctrl_rf_wen = _RAND_12[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_13 = {1{`RANDOM}};
  r_uop_ctrl_csr_cmd = _RAND_13[2:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_14 = {1{`RANDOM}};
  r_uop_ctrl_is_load = _RAND_14[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_15 = {1{`RANDOM}};
  r_uop_ctrl_is_sta = _RAND_15[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_16 = {1{`RANDOM}};
  r_uop_ctrl_is_std = _RAND_16[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_17 = {1{`RANDOM}};
  r_uop_iw_state = _RAND_17[1:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_18 = {1{`RANDOM}};
  r_uop_allocate_brtag = _RAND_18[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_19 = {1{`RANDOM}};
  r_uop_is_br_or_jmp = _RAND_19[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_20 = {1{`RANDOM}};
  r_uop_is_jump = _RAND_20[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_21 = {1{`RANDOM}};
  r_uop_is_jal = _RAND_21[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_22 = {1{`RANDOM}};
  r_uop_is_ret = _RAND_22[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_23 = {1{`RANDOM}};
  r_uop_is_call = _RAND_23[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_24 = {1{`RANDOM}};
  r_uop_br_mask = _RAND_24[7:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_25 = {1{`RANDOM}};
  r_uop_br_tag = _RAND_25[2:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_26 = {1{`RANDOM}};
  r_uop_br_prediction_btb_blame = _RAND_26[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_27 = {1{`RANDOM}};
  r_uop_br_prediction_btb_hit = _RAND_27[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_28 = {1{`RANDOM}};
  r_uop_br_prediction_btb_taken = _RAND_28[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_29 = {1{`RANDOM}};
  r_uop_br_prediction_bpd_blame = _RAND_29[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_30 = {1{`RANDOM}};
  r_uop_br_prediction_bpd_hit = _RAND_30[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_31 = {1{`RANDOM}};
  r_uop_br_prediction_bpd_taken = _RAND_31[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_32 = {1{`RANDOM}};
  r_uop_br_prediction_bim_resp_rowdata = _RAND_32[7:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_33 = {1{`RANDOM}};
  r_uop_br_prediction_bim_resp_entry_idx = _RAND_33[9:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_34 = {1{`RANDOM}};
  r_uop_br_prediction_bpd_resp_takens = _RAND_34[3:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_35 = {3{`RANDOM}};
  r_uop_br_prediction_bpd_resp_history = _RAND_35[89:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_36 = {5{`RANDOM}};
  r_uop_br_prediction_bpd_resp_info = _RAND_36[141:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_37 = {1{`RANDOM}};
  r_uop_stat_brjmp_mispredicted = _RAND_37[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_38 = {1{`RANDOM}};
  r_uop_stat_btb_made_pred = _RAND_38[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_39 = {1{`RANDOM}};
  r_uop_stat_btb_mispredicted = _RAND_39[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_40 = {1{`RANDOM}};
  r_uop_stat_bpd_made_pred = _RAND_40[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_41 = {1{`RANDOM}};
  r_uop_stat_bpd_mispredicted = _RAND_41[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_42 = {1{`RANDOM}};
  r_uop_ftq_idx = _RAND_42[3:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_43 = {1{`RANDOM}};
  r_uop_pc_lob = _RAND_43[5:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_44 = {1{`RANDOM}};
  r_uop_imm_packed = _RAND_44[19:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_45 = {1{`RANDOM}};
  r_uop_csr_addr = _RAND_45[11:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_46 = {1{`RANDOM}};
  r_uop_rob_idx = _RAND_46[6:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_47 = {1{`RANDOM}};
  r_uop_ldq_idx = _RAND_47[3:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_48 = {1{`RANDOM}};
  r_uop_stq_idx = _RAND_48[3:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_49 = {1{`RANDOM}};
  r_uop_pdst = _RAND_49[6:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_50 = {1{`RANDOM}};
  r_uop_pop1 = _RAND_50[6:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_51 = {1{`RANDOM}};
  r_uop_pop2 = _RAND_51[6:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_52 = {1{`RANDOM}};
  r_uop_pop3 = _RAND_52[6:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_53 = {1{`RANDOM}};
  r_uop_prs1_busy = _RAND_53[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_54 = {1{`RANDOM}};
  r_uop_prs2_busy = _RAND_54[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_55 = {1{`RANDOM}};
  r_uop_prs3_busy = _RAND_55[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_56 = {1{`RANDOM}};
  r_uop_stale_pdst = _RAND_56[6:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_57 = {1{`RANDOM}};
  r_uop_exception = _RAND_57[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_58 = {2{`RANDOM}};
  r_uop_exc_cause = _RAND_58[63:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_59 = {1{`RANDOM}};
  r_uop_bypassable = _RAND_59[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_60 = {1{`RANDOM}};
  r_uop_mem_cmd = _RAND_60[4:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_61 = {1{`RANDOM}};
  r_uop_mem_typ = _RAND_61[2:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_62 = {1{`RANDOM}};
  r_uop_is_fence = _RAND_62[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_63 = {1{`RANDOM}};
  r_uop_is_fencei = _RAND_63[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_64 = {1{`RANDOM}};
  r_uop_is_store = _RAND_64[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_65 = {1{`RANDOM}};
  r_uop_is_amo = _RAND_65[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_66 = {1{`RANDOM}};
  r_uop_is_load = _RAND_66[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_67 = {1{`RANDOM}};
  r_uop_is_sys_pc2epc = _RAND_67[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_68 = {1{`RANDOM}};
  r_uop_is_unique = _RAND_68[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_69 = {1{`RANDOM}};
  r_uop_flush_on_commit = _RAND_69[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_70 = {1{`RANDOM}};
  r_uop_ldst = _RAND_70[5:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_71 = {1{`RANDOM}};
  r_uop_lrs1 = _RAND_71[5:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_72 = {1{`RANDOM}};
  r_uop_lrs2 = _RAND_72[5:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_73 = {1{`RANDOM}};
  r_uop_lrs3 = _RAND_73[5:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_74 = {1{`RANDOM}};
  r_uop_ldst_val = _RAND_74[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_75 = {1{`RANDOM}};
  r_uop_dst_rtype = _RAND_75[1:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_76 = {1{`RANDOM}};
  r_uop_lrs1_rtype = _RAND_76[1:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_77 = {1{`RANDOM}};
  r_uop_lrs2_rtype = _RAND_77[1:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_78 = {1{`RANDOM}};
  r_uop_frs3_en = _RAND_78[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_79 = {1{`RANDOM}};
  r_uop_fp_val = _RAND_79[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_80 = {1{`RANDOM}};
  r_uop_fp_single = _RAND_80[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_81 = {1{`RANDOM}};
  r_uop_xcpt_pf_if = _RAND_81[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_82 = {1{`RANDOM}};
  r_uop_xcpt_ae_if = _RAND_82[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_83 = {1{`RANDOM}};
  r_uop_replay_if = _RAND_83[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_84 = {1{`RANDOM}};
  r_uop_xcpt_ma_if = _RAND_84[0:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_85 = {2{`RANDOM}};
  r_uop_debug_wdata = _RAND_85[63:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_86 = {1{`RANDOM}};
  r_uop_debug_events_fetch_seq = _RAND_86[31:0];
  `endif // RANDOMIZE_REG_INIT
  end
`endif // RANDOMIZE
  always @(posedge clock) begin
    if (_T_2730) begin
      r_uop_valid <= io_req_bits_uop_valid;
    end
    if (_T_2730) begin
      r_uop_uopc <= io_req_bits_uop_uopc;
    end
    if (_T_2730) begin
      r_uop_inst <= io_req_bits_uop_inst;
    end
    if (_T_2730) begin
      r_uop_pc <= io_req_bits_uop_pc;
    end
    if (_T_2730) begin
      r_uop_iqtype <= io_req_bits_uop_iqtype;
    end
    if (_T_2730) begin
      r_uop_fu_code <= io_req_bits_uop_fu_code;
    end
    if (_T_2730) begin
      r_uop_ctrl_br_type <= io_req_bits_uop_ctrl_br_type;
    end
    if (_T_2730) begin
      r_uop_ctrl_op1_sel <= io_req_bits_uop_ctrl_op1_sel;
    end
    if (_T_2730) begin
      r_uop_ctrl_op2_sel <= io_req_bits_uop_ctrl_op2_sel;
    end
    if (_T_2730) begin
      r_uop_ctrl_imm_sel <= io_req_bits_uop_ctrl_imm_sel;
    end
    if (_T_2730) begin
      r_uop_ctrl_op_fcn <= io_req_bits_uop_ctrl_op_fcn;
    end
    if (_T_2730) begin
      r_uop_ctrl_fcn_dw <= io_req_bits_uop_ctrl_fcn_dw;
    end
    if (_T_2730) begin
      r_uop_ctrl_rf_wen <= io_req_bits_uop_ctrl_rf_wen;
    end
    if (_T_2730) begin
      r_uop_ctrl_csr_cmd <= io_req_bits_uop_ctrl_csr_cmd;
    end
    if (_T_2730) begin
      r_uop_ctrl_is_load <= io_req_bits_uop_ctrl_is_load;
    end
    if (_T_2730) begin
      r_uop_ctrl_is_sta <= io_req_bits_uop_ctrl_is_sta;
    end
    if (_T_2730) begin
      r_uop_ctrl_is_std <= io_req_bits_uop_ctrl_is_std;
    end
    if (_T_2730) begin
      r_uop_iw_state <= io_req_bits_uop_iw_state;
    end
    if (_T_2730) begin
      r_uop_allocate_brtag <= io_req_bits_uop_allocate_brtag;
    end
    if (_T_2730) begin
      r_uop_is_br_or_jmp <= io_req_bits_uop_is_br_or_jmp;
    end
    if (_T_2730) begin
      r_uop_is_jump <= io_req_bits_uop_is_jump;
    end
    if (_T_2730) begin
      r_uop_is_jal <= io_req_bits_uop_is_jal;
    end
    if (_T_2730) begin
      r_uop_is_ret <= io_req_bits_uop_is_ret;
    end
    if (_T_2730) begin
      r_uop_is_call <= io_req_bits_uop_is_call;
    end
    if (_T_2730) begin
      if (io_brinfo_valid) begin
        r_uop_br_mask <= _T_2737;
      end else begin
        r_uop_br_mask <= io_req_bits_uop_br_mask;
      end
    end else begin
      if (io_brinfo_valid) begin
        r_uop_br_mask <= _T_2745;
      end
    end
    if (_T_2730) begin
      r_uop_br_tag <= io_req_bits_uop_br_tag;
    end
    if (_T_2730) begin
      r_uop_br_prediction_btb_blame <= io_req_bits_uop_br_prediction_btb_blame;
    end
    if (_T_2730) begin
      r_uop_br_prediction_btb_hit <= io_req_bits_uop_br_prediction_btb_hit;
    end
    if (_T_2730) begin
      r_uop_br_prediction_btb_taken <= io_req_bits_uop_br_prediction_btb_taken;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bpd_blame <= io_req_bits_uop_br_prediction_bpd_blame;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bpd_hit <= io_req_bits_uop_br_prediction_bpd_hit;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bpd_taken <= io_req_bits_uop_br_prediction_bpd_taken;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bim_resp_rowdata <= io_req_bits_uop_br_prediction_bim_resp_rowdata;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bim_resp_entry_idx <= io_req_bits_uop_br_prediction_bim_resp_entry_idx;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bpd_resp_takens <= io_req_bits_uop_br_prediction_bpd_resp_takens;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bpd_resp_history <= io_req_bits_uop_br_prediction_bpd_resp_history;
    end
    if (_T_2730) begin
      r_uop_br_prediction_bpd_resp_info <= io_req_bits_uop_br_prediction_bpd_resp_info;
    end
    if (_T_2730) begin
      r_uop_stat_brjmp_mispredicted <= io_req_bits_uop_stat_brjmp_mispredicted;
    end
    if (_T_2730) begin
      r_uop_stat_btb_made_pred <= io_req_bits_uop_stat_btb_made_pred;
    end
    if (_T_2730) begin
      r_uop_stat_btb_mispredicted <= io_req_bits_uop_stat_btb_mispredicted;
    end
    if (_T_2730) begin
      r_uop_stat_bpd_made_pred <= io_req_bits_uop_stat_bpd_made_pred;
    end
    if (_T_2730) begin
      r_uop_stat_bpd_mispredicted <= io_req_bits_uop_stat_bpd_mispredicted;
    end
    if (_T_2730) begin
      r_uop_ftq_idx <= io_req_bits_uop_ftq_idx;
    end
    if (_T_2730) begin
      r_uop_pc_lob <= io_req_bits_uop_pc_lob;
    end
    if (_T_2730) begin
      r_uop_imm_packed <= io_req_bits_uop_imm_packed;
    end
    if (_T_2730) begin
      r_uop_csr_addr <= io_req_bits_uop_csr_addr;
    end
    if (_T_2730) begin
      r_uop_rob_idx <= io_req_bits_uop_rob_idx;
    end
    if (_T_2730) begin
      r_uop_ldq_idx <= io_req_bits_uop_ldq_idx;
    end
    if (_T_2730) begin
      r_uop_stq_idx <= io_req_bits_uop_stq_idx;
    end
    if (_T_2730) begin
      r_uop_pdst <= io_req_bits_uop_pdst;
    end
    if (_T_2730) begin
      r_uop_pop1 <= io_req_bits_uop_pop1;
    end
    if (_T_2730) begin
      r_uop_pop2 <= io_req_bits_uop_pop2;
    end
    if (_T_2730) begin
      r_uop_pop3 <= io_req_bits_uop_pop3;
    end
    if (_T_2730) begin
      r_uop_prs1_busy <= io_req_bits_uop_prs1_busy;
    end
    if (_T_2730) begin
      r_uop_prs2_busy <= io_req_bits_uop_prs2_busy;
    end
    if (_T_2730) begin
      r_uop_prs3_busy <= io_req_bits_uop_prs3_busy;
    end
    if (_T_2730) begin
      r_uop_stale_pdst <= io_req_bits_uop_stale_pdst;
    end
    if (_T_2730) begin
      r_uop_exception <= io_req_bits_uop_exception;
    end
    if (_T_2730) begin
      r_uop_exc_cause <= io_req_bits_uop_exc_cause;
    end
    if (_T_2730) begin
      r_uop_bypassable <= io_req_bits_uop_bypassable;
    end
    if (_T_2730) begin
      r_uop_mem_cmd <= io_req_bits_uop_mem_cmd;
    end
    if (_T_2730) begin
      r_uop_mem_typ <= io_req_bits_uop_mem_typ;
    end
    if (_T_2730) begin
      r_uop_is_fence <= io_req_bits_uop_is_fence;
    end
    if (_T_2730) begin
      r_uop_is_fencei <= io_req_bits_uop_is_fencei;
    end
    if (_T_2730) begin
      r_uop_is_store <= io_req_bits_uop_is_store;
    end
    if (_T_2730) begin
      r_uop_is_amo <= io_req_bits_uop_is_amo;
    end
    if (_T_2730) begin
      r_uop_is_load <= io_req_bits_uop_is_load;
    end
    if (_T_2730) begin
      r_uop_is_sys_pc2epc <= io_req_bits_uop_is_sys_pc2epc;
    end
    if (_T_2730) begin
      r_uop_is_unique <= io_req_bits_uop_is_unique;
    end
    if (_T_2730) begin
      r_uop_flush_on_commit <= io_req_bits_uop_flush_on_commit;
    end
    if (_T_2730) begin
      r_uop_ldst <= io_req_bits_uop_ldst;
    end
    if (_T_2730) begin
      r_uop_lrs1 <= io_req_bits_uop_lrs1;
    end
    if (_T_2730) begin
      r_uop_lrs2 <= io_req_bits_uop_lrs2;
    end
    if (_T_2730) begin
      r_uop_lrs3 <= io_req_bits_uop_lrs3;
    end
    if (_T_2730) begin
      r_uop_ldst_val <= io_req_bits_uop_ldst_val;
    end
    if (_T_2730) begin
      r_uop_dst_rtype <= io_req_bits_uop_dst_rtype;
    end
    if (_T_2730) begin
      r_uop_lrs1_rtype <= io_req_bits_uop_lrs1_rtype;
    end
    if (_T_2730) begin
      r_uop_lrs2_rtype <= io_req_bits_uop_lrs2_rtype;
    end
    if (_T_2730) begin
      r_uop_frs3_en <= io_req_bits_uop_frs3_en;
    end
    if (_T_2730) begin
      r_uop_fp_val <= io_req_bits_uop_fp_val;
    end
    if (_T_2730) begin
      r_uop_fp_single <= io_req_bits_uop_fp_single;
    end
    if (_T_2730) begin
      r_uop_xcpt_pf_if <= io_req_bits_uop_xcpt_pf_if;
    end
    if (_T_2730) begin
      r_uop_xcpt_ae_if <= io_req_bits_uop_xcpt_ae_if;
    end
    if (_T_2730) begin
      r_uop_replay_if <= io_req_bits_uop_replay_if;
    end
    if (_T_2730) begin
      r_uop_xcpt_ma_if <= io_req_bits_uop_xcpt_ma_if;
    end
    if (_T_2730) begin
      r_uop_debug_wdata <= io_req_bits_uop_debug_wdata;
    end
    if (_T_2730) begin
      r_uop_debug_events_fetch_seq <= io_req_bits_uop_debug_events_fetch_seq;
    end
  end
endmodule
