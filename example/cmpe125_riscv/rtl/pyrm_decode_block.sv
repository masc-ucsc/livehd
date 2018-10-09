module pyrm_decode_block(
	reg_addr_pyri,
	reg_addr_valid_pyri,
	reg_addr_retry_pyro,
	reg_data_pyri,
	reg_data_valid_pyri,
	reg_data_retry_pyro,
	inst_pyri,
	inst_valid_pyri,
	inst_retry_pyro,
	pc_pyri,
	pc_valid_pyri,
	pc_retry_pyro,
	reset_pyri,
	inst_pyro,
	inst_valid_pyro,
	inst_retry_pyri,
	pc_pyro,
	pc_valid_pyro,
	pc_retry_pyri,
	src1_pyro,
	src1_valid_pyro,
	src1_retry_pyri,
	src2_pyro,
	src2_valid_pyro,
	src2_retry_pyri,
	clk
);


  

  

  

  

  

  


input [64-1:0] reg_addr_pyri;
input reg_addr_valid_pyri;
output reg_addr_retry_pyro;
input [64-1:0] reg_data_pyri;
input reg_data_valid_pyri;
output reg_data_retry_pyro;
input [32-1:0] inst_pyri;
input inst_valid_pyri;
output inst_retry_pyro;
input [64-1:0] pc_pyri;
input pc_valid_pyri;
output pc_retry_pyro;
input  reset_pyri;
output[32-1:0] inst_pyro;
output inst_valid_pyro;
input inst_retry_pyri;
output[64-1:0] pc_pyro;
output pc_valid_pyro;
input pc_retry_pyri;
output[64-1:0] src1_pyro;
output src1_valid_pyro;
input src1_retry_pyri;
output[64-1:0] src2_pyro;
output src2_valid_pyro;
input src2_retry_pyri;
input clk;
logic [64-1:0] __tid17235;
logic inst__keep__4594;
logic src2__write__4595;
logic src1__write__4596;
logic pc__write__4597;
logic inst__write__4598;
logic data__write__4599;
logic pc__read__4602;
logic [32-1:0] raw__4634;
logic __tid3131__4645;
logic [64-1:0] __tid3144__4651;
logic __tid3150__4654;
logic __tid3152__4655;
logic [64-1:0] reg_addr__4664;
logic [32-1:0] raw__4671;
logic [64-1:0] reg_addr__4672;
logic [32-1:0] raw__4673;
logic [32-1:0] raw__4686;
logic __tid3189__4711;
logic [7-1:0] __tid3197__4716;
logic [7-1:0] __tid3218__4727;
logic __tid3222__4729;
logic __tid3248__4730;
logic [7-1:0] __tid3224__4733;
logic __tid3228__4736;
logic __tid3228__4741;
logic __tid3228__4742;
logic __tid3230__4743;
logic __tid3254__4744;
logic [7-1:0] __tid3232__4747;
logic __tid3236__4750;
logic __tid3236__4755;
logic __tid3236__4756;
logic __tid3238__4757;
logic __tid3260__4758;
logic [7-1:0] __tid3240__4761;
logic __tid3244__4764;
logic __tid3244__4769;
logic __tid3244__4770;
logic __tid3266__4771;
logic __tid3268__4772;
logic [7-1:0] __tid3282__4779;
logic __tid3286__4781;
logic __tid3304__4782;
logic [7-1:0] __tid3288__4785;
logic __tid3292__4788;
logic __tid3292__4793;
logic __tid3292__4794;
logic __tid3294__4795;
logic __tid3310__4796;
logic [7-1:0] __tid3296__4799;
logic __tid3300__4802;
logic __tid3300__4807;
logic __tid3300__4808;
logic __tid3316__4809;
logic __tid3318__4810;
logic [7-1:0] __tid3324__4813;
logic __tid3330__4815;
logic __tid3332__4816;
logic [7-1:0] __tid3338__4819;
logic __tid3344__4821;
logic __tid3346__4822;
logic [7-1:0] __tid3356__4827;
logic __tid3360__4829;
logic __tid3370__4830;
logic [7-1:0] __tid3362__4833;
logic __tid3366__4836;
logic __tid3366__4841;
logic __tid3366__4842;
logic __tid3376__4843;
logic __tid3378__4844;
logic [7-1:0] __tid3384__4847;
logic __tid3390__4849;
logic __tid3392__4850;
logic __tid3394__4851;
logic __tid3396__4852;
logic __tid3398__4853;
logic __tid3400__4854;
logic __tid3402__4855;
logic __tid3404__4856;
logic __tid3406__4857;
logic __tid3408__4858;
logic __tid3410__4859;
logic __tid3412__4860;
logic [32-1:0] inst__4864;
logic [5-1:0] __tid3420__4865;
logic [5-1:0] ra1__4867;
logic [32-1:0] inst__4870;
logic [5-1:0] __tid3431__4871;
logic [5-1:0] ra2__4873;
logic __tid3458__4893;
logic [32-1:0] inst__4901;
logic [5-1:0] __tid3483__4902;
logic [7-1:0] __tid3489__4908;
logic __tid3493__4910;
logic __tid3499__4920;
logic inst__keep__4927;
logic [5-1:0] __tid3538__4944;
logic __tid3542__4946;
logic __tid3542__4951;
logic [64-1:0] __tid3546__4956;
logic reg_addr__read__4957;
logic [5-1:0] __tid3548__4959;
logic __tid3550__4960;
logic __tid3550__4967;
logic reg_data__read__4975;
logic [5-1:0] ra1__4977;
logic [5-1:0] __tid3592__4995;
logic __tid3596__4997;
logic __tid3596__5002;
logic [64-1:0] __tid3600__5007;
logic reg_addr__read__5008;
logic [5-1:0] __tid3602__5010;
logic __tid3604__5011;
logic __tid3604__5017;
logic reg_data__read__5025;
logic [5-1:0] ra2__5027;
logic [7-1:0] __tid3645__5043;
logic __tid3649__5045;
logic __tid3659__5046;
logic [7-1:0] __tid3651__5049;
logic __tid3655__5052;
logic __tid3655__5057;
logic __tid3655__5058;
logic __tid3665__5059;
logic __tid3667__5060;
logic [32-1:0] inst__5064;
logic [5-1:0] __tid3678__5065;
logic inst__write__5084;
logic pc__write__5089;
logic pc__read__5091;
logic src1__write__5112;
logic src2__write__5135;
logic __tid3396__5218;
logic [32-1:0] inst__5222;
logic [5-1:0] __tid3698__5223;
logic [5-1:0] ra1__5225;
logic [32-1:0] inst__5231;
logic [5-1:0] __tid3716__5232;
logic __tid3732__5250;
logic [32-1:0] inst__5257;
logic [5-1:0] __tid3766__5269;
logic __tid3770__5271;
logic __tid3770__5276;
logic [64-1:0] __tid3774__5281;
logic reg_addr__read__5282;
logic [5-1:0] __tid3776__5284;
logic __tid3778__5285;
logic __tid3778__5292;
logic reg_data__read__5300;
logic [5-1:0] ra1__5302;
logic [12-1:0] __tid3817__5317;
logic [64-1:0] src2__5319;
logic [6-1:0] __tid3825__5321;
logic __tid3827__5322;
logic __tid3829__5324;
logic __tid3835__5326;
logic __tid3837__5327;
logic [64-1:0] src2__5331;
logic [64-1:0] src2__5332;
logic [64-1:0] src2__5337;
logic [5-1:0] __tid3859__5342;
logic [64-1:0] src1__5377;
logic [64-1:0] src2__5388;
logic src2__write__5390;
logic __tid3400__5415;
logic [32-1:0] inst__5419;
logic [5-1:0] __tid3873__5420;
logic [5-1:0] ra1__5422;
logic [32-1:0] inst__5428;
logic [5-1:0] __tid3891__5429;
logic __tid3907__5447;
logic [32-1:0] inst__5454;
logic [5-1:0] __tid3941__5463;
logic [5-1:0] __tid3954__5474;
logic __tid3958__5476;
logic __tid3958__5481;
logic [64-1:0] __tid3962__5486;
logic reg_addr__read__5487;
logic [5-1:0] __tid3964__5489;
logic __tid3966__5490;
logic __tid3966__5497;
logic reg_data__read__5505;
logic [5-1:0] ra1__5507;
logic [64-1:0] src1__5557;
logic __tid3404__5564;
logic [32-1:0] inst__5568;
logic [5-1:0] __tid4010__5569;
logic __tid3408__5599;
logic [32-1:0] inst__5603;
logic [5-1:0] __tid4048__5604;
logic [32-1:0] inst__5616;
logic [20-1:0] __tid4084__5626;
logic [5-1:0] __tid4098__5632;
logic inst__keep__5638;
logic src1__write__5659;
logic inst__read__6498;
logic __tid5976__6518;
logic __tid5978__6519;
logic __tid5998__6529;
logic __tid6004__6532;
logic __tid6010__6535;
logic __tid6016__6538;
logic __tid3152__17331;
logic __tid16148__17355;
logic [64-1:0] __tid16150__17356;
logic [64-1:0] __tid16152__17357;
logic [64-1:0] __tid16170__17358;
logic [64-1:0] __tid16405__17360;
logic __tid16407__17361;
logic [64-1:0] __tid16415__17363;
logic __tid16417__17364;
logic [64-1:0] __tid16426__17366;
logic __tid16428__17367;
logic [64-1:0] __tid16462__17369;
logic __tid16464__17370;
logic [64-1:0] __tid16485__17372;
logic __tid3129__4641;
logic __tid3131__4646;
logic __tid3139__4647;
logic __tid3141__4648;
logic data__write__4704;
logic [32-1:0] raw__4706;
logic [32-1:0] raw__4874;
logic __tid3440__4875;
logic [32-1:0] raw__4877;
logic __tid3446__4878;
logic __tid3448__4880;
logic __tid3452__4882;
logic __tid3462__4883;
logic __tid3454__4886;
logic __tid3458__4888;
logic __tid3458__4894;
logic __tid3468__4895;
logic __tid3470__4896;
logic [32-1:0] raw__4904;
logic __tid3487__4905;
logic __tid3495__4913;
logic __tid3499__4915;
logic __tid3499__4921;
logic __tid3507__4922;
logic __tid3509__4923;
logic __tid3511__4924;
logic __tid3513__4925;
logic __tid3513__4928;
logic __tid3536__4941;
logic __tid3542__4952;
logic __tid3544__4953;
logic reg_addr__read__4964;
logic __tid3550__4968;
logic __tid3562__4969;
logic __tid3564__4970;
logic reg_data__read__4985;
logic __tid3590__4992;
logic __tid3596__5003;
logic __tid3598__5004;
logic reg_addr__read__5014;
logic __tid3604__5018;
logic __tid3616__5019;
logic __tid3618__5020;
logic reg_data__read__5034;
logic [32-1:0] raw__5067;
logic [32-1:0] raw__5068;
logic [32-1:0] raw__5076;
logic reg_addr__read__5103;
logic reg_data__read__5114;
logic [32-1:0] raw__5159;
logic inst__keep__5162;
logic reg_addr__read__5176;
logic reg_data__read__5184;
logic [32-1:0] raw__5215;
logic [32-1:0] raw__5226;
logic __tid3707__5227;
logic [32-1:0] raw__5234;
logic __tid3720__5235;
logic __tid3722__5237;
logic __tid3726__5239;
logic __tid3736__5240;
logic __tid3728__5243;
logic __tid3732__5245;
logic __tid3732__5251;
logic __tid3742__5252;
logic __tid3744__5253;
logic __tid3764__5266;
logic __tid3770__5277;
logic __tid3772__5278;
logic reg_addr__read__5289;
logic __tid3778__5293;
logic __tid3790__5294;
logic __tid3792__5295;
logic reg_data__read__5310;
logic [32-1:0] raw__5344;
logic [32-1:0] raw__5345;
logic reg_addr__read__5369;
logic reg_data__read__5381;
logic [64-1:0] src2__5389;
logic [32-1:0] raw__5411;
logic [32-1:0] raw__5423;
logic __tid3882__5424;
logic [32-1:0] raw__5431;
logic __tid3895__5432;
logic __tid3897__5434;
logic __tid3901__5436;
logic __tid3911__5437;
logic __tid3903__5440;
logic __tid3907__5442;
logic __tid3907__5448;
logic __tid3917__5449;
logic __tid3919__5450;
logic [32-1:0] raw__5465;
logic [32-1:0] raw__5466;
logic __tid3952__5471;
logic __tid3958__5482;
logic __tid3960__5483;
logic reg_addr__read__5494;
logic __tid3966__5498;
logic __tid3978__5499;
logic __tid3980__5500;
logic reg_data__read__5515;
logic [32-1:0] raw__5535;
logic reg_addr__read__5549;
logic reg_data__read__5561;
logic [32-1:0] raw__5571;
logic __tid4014__5572;
logic __tid4016__5574;
logic __tid4022__5576;
logic __tid4024__5577;
logic [32-1:0] raw__5606;
logic __tid4052__5607;
logic __tid4054__5609;
logic __tid4060__5611;
logic __tid4062__5612;
logic [32-1:0] raw__5634;
logic [32-1:0] raw__5635;
logic [64-1:0] src1__5658;
logic [32-1:0] raw__5665;
logic inst__keep__5706;
logic [64-1:0] src1__5716;
logic [32-1:0] raw__5720;
logic inst__keep__5738;
logic [64-1:0] src1__5755;
logic [32-1:0] raw__5760;
logic inst__keep__5793;
logic [32-1:0] raw__5801;
logic reg_addr__read__5810;
logic reg_data__read__5817;
logic inst__keep__5871;
logic reg_addr__read__5884;
logic reg_data__read__5891;
logic [64-1:0] src2__5896;
logic [32-1:0] raw__5908;
logic inst__keep__6017;
logic reg_addr__read__6030;
logic reg_data__read__6037;
logic [32-1:0] raw__6072;
logic [32-1:0] raw__6314;
logic inst__keep__6339;
logic reg_addr__read__6351;
logic reg_data__read__6358;
logic inst__keep__6489;
logic data__write__6494;
logic reg_data__read__6500;
logic reg_addr__read__6501;
logic __tid5968__6514;
logic __tid5970__6515;
logic __tid5972__6516;
logic __tid5974__6517;
logic __tid6021__6540;
logic __tid6023__6541;
logic __tid3513__17332;
logic __tid3513__17333;
logic __tid3513__17334;
logic __tid3513__17335;
logic __tid3513__17336;
logic __tid3744__17337;
logic __tid3744__17338;
logic __tid3744__17339;
logic __tid3744__17340;
logic __tid3744__17341;
logic __tid3919__17342;
logic __tid3919__17343;
logic __tid3919__17344;
logic __tid3919__17345;
logic __tid4024__17346;
logic __tid4024__17347;
logic __tid4024__17348;
logic __tid4062__17349;
logic __tid4062__17350;
logic __tid4062__17351;
logic __tid4062__17352;
logic [64-1:0] __tid16172__17359;
logic [64-1:0] __tid16409__17362;
logic [64-1:0] __tid16419__17365;
logic [64-1:0] __tid16430__17368;
logic [64-1:0] __tid16466__17371;
logic [64-1:0] src1__4982;
logic [64-1:0] src2__5032;
logic inst__write__5164;
logic pc__write__5167;
logic pc__read__5168;
logic src1__write__5183;
logic src2__write__5200;
logic [64-1:0] src1__5307;
logic [64-1:0] src1__5378;
logic [64-1:0] src1__5512;
logic [64-1:0] src1__5558;
logic inst__write__5708;
logic pc__write__5710;
logic pc__read__5711;
logic src1__write__5717;
logic inst__write__5740;
logic pc__write__5742;
logic pc__read__5743;
logic src1__write__5756;
logic inst__write__5795;
logic pc__write__5797;
logic pc__read__5798;
logic [64-1:0] src1__5815;
logic src1__write__5816;
logic inst__write__5873;
logic pc__write__5875;
logic pc__read__5876;
logic [64-1:0] src1__5889;
logic src1__write__5890;
logic src2__write__5897;
logic inst__write__6019;
logic pc__write__6021;
logic pc__read__6022;
logic [64-1:0] src1__6035;
logic src1__write__6036;
logic [64-1:0] src2__6054;
logic src2__write__6055;
logic inst__write__6340;
logic pc__write__6342;
logic pc__read__6343;
logic src1__write__6357;
logic src2__write__6376;
logic src2__write__6490;
logic src1__write__6491;
logic pc__write__6492;
logic inst__write__6493;
logic pc__read__6497;
logic __tid5980__6520;
logic __tid5982__6521;
logic __tid5996__6528;
logic __tid6000__6530;
logic __tid6002__6531;
logic __tid6006__6533;
logic __tid6008__6534;
logic __tid6012__6536;
logic __tid6014__6537;
logic __tid6018__6539;
logic __tid6025__6542;
logic __tid6029__6544;
logic __tid6031__6545;
logic __tid6033__6546;
logic __tid6035__6547;
logic __tid6037__6548;
logic __block_valid__6549;
logic __tid6045__6560;
logic __tid6047__6561;
logic __tid6052__6564;
logic __tid6065__6571;
logic __tid6069__6573;
logic __tid6073__6575;
logic __tid6075__6576;
logic __tid6079__6578;
logic [64-1:0] __tid16487__17373;
logic [64-1:0] reg_addr_pyri;
logic reg_addr_retry_pyro;
logic reg_addr_valid_pyri;
logic [64-1:0] reg_data_pyri;
logic reg_data_retry_pyro;
logic reg_data_valid_pyri;
logic [32-1:0] inst_pyri;
logic inst_retry_pyro;
logic inst_valid_pyri;
logic [64-1:0] pc_pyri;
logic pc_retry_pyro;
logic pc_valid_pyri;
logic reset_pyri;
logic reset_retry_pyro;
logic reset_valid_pyri;
logic [32-1:0] raw_pyri;
logic [64-1:0] data_pyri;
logic [32-1:0] inst_pyro;
logic inst_valid_pyro;
logic inst_retry_pyri;
logic [64-1:0] pc_pyro;
logic pc_valid_pyro;
logic pc_retry_pyri;
logic [64-1:0] src1_pyro;
logic src1_valid_pyro;
logic src1_retry_pyri;
logic [64-1:0] src2_pyro;
logic src2_valid_pyro;
logic src2_retry_pyri;
logic [32-1:0] raw_pyro;
logic raw_valid_pyro;
logic [64-1:0] data_pyro;
logic data_valid_pyro;



  
    logic[31:0] raw /*verilator public*/;
  

  
    logic[63:0] data[32-1:0];
  



assign inst__keep__4594 = 0; // asn inst__keep__4594, false

assign src2__write__4595 = 0; // asn src2__write__4595, false

assign src1__write__4596 = 0; // asn src1__write__4596, false

assign pc__write__4597 = 0; // asn pc__write__4597, false

assign inst__write__4598 = 0; // asn inst__write__4598, false

assign data__write__4599 = 0; // asn data__write__4599, false

assign pc__read__4602 = 0; // asn pc__read__4602, false

assign raw__4634 = (raw & ~(32'd0 | (1 << 32'd0))) | ((32'd0 & 1) << 32'd0);

assign __tid3131__4645 = 0; // asn __tid3131__4645, false

assign __tid3144__4651 = reg_addr_pyri; // asn __tid3144__4651, reg_addr_pyri

assign __tid3150__4654 = __tid3144__4651 == 64'd0; // asn __tid3150__4654, %is_equal(__tid3144__4651, 0<uint<64>>)

assign __tid3152__4655 = __tid3150__4654; // asn __tid3152__4655, __tid3150__4654

assign reg_addr__4664 = reg_addr_pyri; // asn reg_addr__4664, reg_addr_pyri

assign raw__4671 = raw__4634; // asn raw__4671, raw__4634

assign reg_addr__4672 = reg_addr_pyri; // asn reg_addr__4672, reg_addr_pyri

assign raw__4673 = (raw__4671 & ~(32'd0 | (1 << reg_addr__4672[4:0]))) | ((32'd0 & 1) << reg_addr__4672[4:0]);

assign raw__4686 = (__tid3152__4655) ? raw__4634 : raw__4673;

assign __tid3189__4711 = inst_valid_pyri; // asn __tid3189__4711, inst_valid_pyri

assign __tid3197__4716 = inst_pyri[6:0]; // asn __tid3197__4716, %bit_read(inst_pyri, 6<uint<4>>, 0<uint<1>>)

assign __tid3218__4727 = __tid3197__4716; // asn __tid3218__4727, __tid3197__4716

assign __tid3222__4729 = __tid3218__4727 == 7'd51; // asn __tid3222__4729, %is_equal(__tid3218__4727, 51<uint<7>>)

assign __tid3248__4730 = ! __tid3222__4729; // asn __tid3248__4730, %logical_not(__tid3222__4729)

assign __tid3224__4733 = __tid3197__4716; // asn __tid3224__4733, __tid3197__4716

assign __tid3228__4736 = __tid3224__4733 == 7'd59; // asn __tid3228__4736, %is_equal(__tid3224__4733, 59<uint<7>>)

assign __tid3228__4741 = 0; // asn __tid3228__4741, false

assign __tid3228__4742 = (__tid3248__4730) ? __tid3228__4736 : __tid3228__4741;

assign __tid3230__4743 = __tid3222__4729 | __tid3228__4742; // asn __tid3230__4743, %logical_or(__tid3222__4729, __tid3228__4742)

assign __tid3254__4744 = ! __tid3230__4743; // asn __tid3254__4744, %logical_not(__tid3230__4743)

assign __tid3232__4747 = __tid3197__4716; // asn __tid3232__4747, __tid3197__4716

assign __tid3236__4750 = __tid3232__4747 == 7'd99; // asn __tid3236__4750, %is_equal(__tid3232__4747, 99<uint<7>>)

assign __tid3236__4755 = 0; // asn __tid3236__4755, false

assign __tid3236__4756 = (__tid3254__4744) ? __tid3236__4750 : __tid3236__4755;

assign __tid3238__4757 = __tid3230__4743 | __tid3236__4756; // asn __tid3238__4757, %logical_or(__tid3230__4743, __tid3236__4756)

assign __tid3260__4758 = ! __tid3238__4757; // asn __tid3260__4758, %logical_not(__tid3238__4757)

assign __tid3240__4761 = __tid3197__4716; // asn __tid3240__4761, __tid3197__4716

assign __tid3244__4764 = __tid3240__4761 == 7'd35; // asn __tid3244__4764, %is_equal(__tid3240__4761, 35<uint<7>>)

assign __tid3244__4769 = 0; // asn __tid3244__4769, false

assign __tid3244__4770 = (__tid3260__4758) ? __tid3244__4764 : __tid3244__4769;

assign __tid3266__4771 = __tid3238__4757 | __tid3244__4770; // asn __tid3266__4771, %logical_or(__tid3238__4757, __tid3244__4770)

assign __tid3268__4772 = __tid3266__4771; // asn __tid3268__4772, __tid3266__4771

assign __tid3282__4779 = __tid3197__4716; // asn __tid3282__4779, __tid3197__4716

assign __tid3286__4781 = __tid3282__4779 == 7'd19; // asn __tid3286__4781, %is_equal(__tid3282__4779, 19<uint<7>>)

assign __tid3304__4782 = ! __tid3286__4781; // asn __tid3304__4782, %logical_not(__tid3286__4781)

assign __tid3288__4785 = __tid3197__4716; // asn __tid3288__4785, __tid3197__4716

assign __tid3292__4788 = __tid3288__4785 == 7'd27; // asn __tid3292__4788, %is_equal(__tid3288__4785, 27<uint<7>>)

assign __tid3292__4793 = 0; // asn __tid3292__4793, false

assign __tid3292__4794 = (__tid3304__4782) ? __tid3292__4788 : __tid3292__4793;

assign __tid3294__4795 = __tid3286__4781 | __tid3292__4794; // asn __tid3294__4795, %logical_or(__tid3286__4781, __tid3292__4794)

assign __tid3310__4796 = ! __tid3294__4795; // asn __tid3310__4796, %logical_not(__tid3294__4795)

assign __tid3296__4799 = __tid3197__4716; // asn __tid3296__4799, __tid3197__4716

assign __tid3300__4802 = __tid3296__4799 == 7'd3; // asn __tid3300__4802, %is_equal(__tid3296__4799, 3<uint<7>>)

assign __tid3300__4807 = 0; // asn __tid3300__4807, false

assign __tid3300__4808 = (__tid3310__4796) ? __tid3300__4802 : __tid3300__4807;

assign __tid3316__4809 = __tid3294__4795 | __tid3300__4808; // asn __tid3316__4809, %logical_or(__tid3294__4795, __tid3300__4808)

assign __tid3318__4810 = __tid3316__4809; // asn __tid3318__4810, __tid3316__4809

assign __tid3324__4813 = __tid3197__4716; // asn __tid3324__4813, __tid3197__4716

assign __tid3330__4815 = __tid3324__4813 == 7'd103; // asn __tid3330__4815, %is_equal(__tid3324__4813, 103<uint<7>>)

assign __tid3332__4816 = __tid3330__4815; // asn __tid3332__4816, __tid3330__4815

assign __tid3338__4819 = __tid3197__4716; // asn __tid3338__4819, __tid3197__4716

assign __tid3344__4821 = __tid3338__4819 == 7'd111; // asn __tid3344__4821, %is_equal(__tid3338__4819, 111<uint<7>>)

assign __tid3346__4822 = __tid3344__4821; // asn __tid3346__4822, __tid3344__4821

assign __tid3356__4827 = __tid3197__4716; // asn __tid3356__4827, __tid3197__4716

assign __tid3360__4829 = __tid3356__4827 == 7'd55; // asn __tid3360__4829, %is_equal(__tid3356__4827, 55<uint<7>>)

assign __tid3370__4830 = ! __tid3360__4829; // asn __tid3370__4830, %logical_not(__tid3360__4829)

assign __tid3362__4833 = __tid3197__4716; // asn __tid3362__4833, __tid3197__4716

assign __tid3366__4836 = __tid3362__4833 == 7'd23; // asn __tid3366__4836, %is_equal(__tid3362__4833, 23<uint<7>>)

assign __tid3366__4841 = 0; // asn __tid3366__4841, false

assign __tid3366__4842 = (__tid3370__4830) ? __tid3366__4836 : __tid3366__4841;

assign __tid3376__4843 = __tid3360__4829 | __tid3366__4842; // asn __tid3376__4843, %logical_or(__tid3360__4829, __tid3366__4842)

assign __tid3378__4844 = __tid3376__4843; // asn __tid3378__4844, __tid3376__4843

assign __tid3384__4847 = __tid3197__4716; // asn __tid3384__4847, __tid3197__4716

assign __tid3390__4849 = __tid3384__4847 == 7'd115; // asn __tid3390__4849, %is_equal(__tid3384__4847, 115<uint<7>>)

assign __tid3392__4850 = __tid3390__4849; // asn __tid3392__4850, __tid3390__4849

assign __tid3394__4851 = ! __tid3268__4772; // asn __tid3394__4851, %logical_not(__tid3268__4772)

assign __tid3396__4852 = __tid3394__4851 & __tid3318__4810; // asn __tid3396__4852, %logical_and(__tid3394__4851, __tid3318__4810)

assign __tid3398__4853 = ! __tid3396__4852; // asn __tid3398__4853, %logical_not(__tid3396__4852)

assign __tid3400__4854 = __tid3398__4853 & __tid3332__4816; // asn __tid3400__4854, %logical_and(__tid3398__4853, __tid3332__4816)

assign __tid3402__4855 = ! __tid3400__4854; // asn __tid3402__4855, %logical_not(__tid3400__4854)

assign __tid3404__4856 = __tid3402__4855 & __tid3346__4822; // asn __tid3404__4856, %logical_and(__tid3402__4855, __tid3346__4822)

assign __tid3406__4857 = ! __tid3404__4856; // asn __tid3406__4857, %logical_not(__tid3404__4856)

assign __tid3408__4858 = __tid3406__4857 & __tid3378__4844; // asn __tid3408__4858, %logical_and(__tid3406__4857, __tid3378__4844)

assign __tid3410__4859 = ! __tid3408__4858; // asn __tid3410__4859, %logical_not(__tid3408__4858)

assign __tid3412__4860 = __tid3410__4859 & __tid3392__4850; // asn __tid3412__4860, %logical_and(__tid3410__4859, __tid3392__4850)

assign inst__4864 = inst_pyri; // asn inst__4864, inst_pyri

assign __tid3420__4865 = inst__4864[19:15]; // asn __tid3420__4865, %bit_read(inst__4864, 19<uint<6>>, 15<uint<5>>)

assign ra1__4867 = __tid3420__4865; // asn ra1__4867, __tid3420__4865

assign inst__4870 = inst_pyri; // asn inst__4870, inst_pyri

assign __tid3431__4871 = inst__4870[24:20]; // asn __tid3431__4871, %bit_read(inst__4870, 24<uint<6>>, 20<uint<6>>)

assign ra2__4873 = __tid3431__4871; // asn ra2__4873, __tid3431__4871

assign __tid3458__4893 = 0; // asn __tid3458__4893, false

assign inst__4901 = inst_pyri; // asn inst__4901, inst_pyri

assign __tid3483__4902 = inst__4901[11:7]; // asn __tid3483__4902, %bit_read(inst__4901, 11<uint<5>>, 7<uint<4>>)

assign __tid3489__4908 = __tid3197__4716; // asn __tid3489__4908, __tid3197__4716

assign __tid3493__4910 = __tid3489__4908 == 7'd99; // asn __tid3493__4910, %is_equal(__tid3489__4908, 99<uint<7>>)

assign __tid3499__4920 = 0; // asn __tid3499__4920, false

assign inst__keep__4927 = 1; // asn inst__keep__4927, true

assign __tid3538__4944 = __tid3420__4865; // asn __tid3538__4944, __tid3420__4865

assign __tid3542__4946 = __tid3538__4944 != 5'd0; // asn __tid3542__4946, %is_not_equal(__tid3538__4944, 0<uint<5>>)

assign __tid3542__4951 = 0; // asn __tid3542__4951, false

assign __tid3546__4956 = reg_addr_pyri; // asn __tid3546__4956, reg_addr_pyri

assign reg_addr__read__4957 = 1; // asn reg_addr__read__4957, true

assign __tid3548__4959 = __tid3420__4865; // asn __tid3548__4959, __tid3420__4865

assign __tid3550__4960 = __tid3546__4956 == { 59'd0, __tid3548__4959 }; // asn __tid3550__4960, %is_equal(__tid3546__4956, __tid3548__4959)

assign __tid3550__4967 = 0; // asn __tid3550__4967, false

assign reg_data__read__4975 = 1; // asn reg_data__read__4975, true

assign ra1__4977 = __tid3420__4865; // asn ra1__4977, __tid3420__4865

assign __tid3592__4995 = __tid3431__4871; // asn __tid3592__4995, __tid3431__4871

assign __tid3596__4997 = __tid3592__4995 != 5'd0; // asn __tid3596__4997, %is_not_equal(__tid3592__4995, 0<uint<5>>)

assign __tid3596__5002 = 0; // asn __tid3596__5002, false

assign __tid3600__5007 = reg_addr_pyri; // asn __tid3600__5007, reg_addr_pyri

assign reg_addr__read__5008 = 1; // asn reg_addr__read__5008, true

assign __tid3602__5010 = __tid3431__4871; // asn __tid3602__5010, __tid3431__4871

assign __tid3604__5011 = __tid3600__5007 == { 59'd0, __tid3602__5010 }; // asn __tid3604__5011, %is_equal(__tid3600__5007, __tid3602__5010)

assign __tid3604__5017 = 0; // asn __tid3604__5017, false

assign reg_data__read__5025 = 1; // asn reg_data__read__5025, true

assign ra2__5027 = __tid3431__4871; // asn ra2__5027, __tid3431__4871

assign __tid3645__5043 = __tid3197__4716; // asn __tid3645__5043, __tid3197__4716

assign __tid3649__5045 = __tid3645__5043 == 7'd51; // asn __tid3649__5045, %is_equal(__tid3645__5043, 51<uint<7>>)

assign __tid3659__5046 = ! __tid3649__5045; // asn __tid3659__5046, %logical_not(__tid3649__5045)

assign __tid3651__5049 = __tid3197__4716; // asn __tid3651__5049, __tid3197__4716

assign __tid3655__5052 = __tid3651__5049 == 7'd59; // asn __tid3655__5052, %is_equal(__tid3651__5049, 59<uint<7>>)

assign __tid3655__5057 = 0; // asn __tid3655__5057, false

assign __tid3655__5058 = (__tid3659__5046) ? __tid3655__5052 : __tid3655__5057;

assign __tid3665__5059 = __tid3649__5045 | __tid3655__5058; // asn __tid3665__5059, %logical_or(__tid3649__5045, __tid3655__5058)

assign __tid3667__5060 = __tid3665__5059; // asn __tid3667__5060, __tid3665__5059

assign inst__5064 = inst_pyri; // asn inst__5064, inst_pyri

assign __tid3678__5065 = inst__5064[11:7]; // asn __tid3678__5065, %bit_read(inst__5064, 11<uint<5>>, 7<uint<4>>)

assign inst__write__5084 = 0; // asn inst__write__5084, false

assign pc__write__5089 = 0; // asn pc__write__5089, false

assign pc__read__5091 = 0; // asn pc__read__5091, false

assign src1__write__5112 = 0; // asn src1__write__5112, false

assign src2__write__5135 = 0; // asn src2__write__5135, false

assign __tid3396__5218 = __tid3396__4852; // asn __tid3396__5218, __tid3396__4852

assign inst__5222 = inst_pyri; // asn inst__5222, inst_pyri

assign __tid3698__5223 = inst__5222[19:15]; // asn __tid3698__5223, %bit_read(inst__5222, 19<uint<6>>, 15<uint<5>>)

assign ra1__5225 = __tid3698__5223; // asn ra1__5225, __tid3698__5223

assign inst__5231 = inst_pyri; // asn inst__5231, inst_pyri

assign __tid3716__5232 = inst__5231[11:7]; // asn __tid3716__5232, %bit_read(inst__5231, 11<uint<5>>, 7<uint<4>>)

assign __tid3732__5250 = 0; // asn __tid3732__5250, false

assign inst__5257 = inst_pyri; // asn inst__5257, inst_pyri

assign __tid3766__5269 = __tid3698__5223; // asn __tid3766__5269, __tid3698__5223

assign __tid3770__5271 = __tid3766__5269 != 5'd0; // asn __tid3770__5271, %is_not_equal(__tid3766__5269, 0<uint<5>>)

assign __tid3770__5276 = 0; // asn __tid3770__5276, false

assign __tid3774__5281 = reg_addr_pyri; // asn __tid3774__5281, reg_addr_pyri

assign reg_addr__read__5282 = 1; // asn reg_addr__read__5282, true

assign __tid3776__5284 = __tid3698__5223; // asn __tid3776__5284, __tid3698__5223

assign __tid3778__5285 = __tid3774__5281 == { 59'd0, __tid3776__5284 }; // asn __tid3778__5285, %is_equal(__tid3774__5281, __tid3776__5284)

assign __tid3778__5292 = 0; // asn __tid3778__5292, false

assign reg_data__read__5300 = 1; // asn reg_data__read__5300, true

assign ra1__5302 = __tid3698__5223; // asn ra1__5302, __tid3698__5223

assign __tid3817__5317 = inst__5257[31:20]; // asn __tid3817__5317, %bit_read(inst__5257, 31<uint<6>>, 20<uint<6>>)

assign src2__5319 = { 52'd0, __tid3817__5317 }; // asn src2__5319, __tid3817__5317

assign __tid3825__5321 = 6'd11; // asn __tid3825__5321, 11<uint<6>>

assign __tid3827__5322 = src2__5319[__tid3825__5321]; // asn __tid3827__5322, %bit_read(src2__5319, __tid3825__5321)

assign __tid3829__5324 = __tid3827__5322; // asn __tid3829__5324, __tid3827__5322

assign __tid3835__5326 = __tid3829__5324 == 1'd1; // asn __tid3835__5326, %is_equal(__tid3829__5324, 1<uint<1>>)

assign __tid3837__5327 = __tid3835__5326; // asn __tid3837__5327, __tid3835__5326

assign src2__5331 = { 52'd0, __tid3817__5317 }; // asn src2__5331, __tid3817__5317

assign src2__5332 = src2__5331 | 64'd18446744073709547520; // asn src2__5332, %numeric_or(src2__5331, 18446744073709547520<uint<64>>)

assign src2__5337 = (__tid3837__5327) ? src2__5332 : { 52'd0, __tid3817__5317 };

assign __tid3859__5342 = inst__5257[11:7]; // asn __tid3859__5342, %bit_read(inst__5257, 11<uint<5>>, 7<uint<4>>)

assign src1__5377 = 64'd0; // asn src1__5377, 0<uint<1>>

assign src2__5388 = 64'd0; // asn src2__5388, 0<uint<1>>

assign src2__write__5390 = 0; // asn src2__write__5390, false

assign __tid3400__5415 = __tid3400__4854; // asn __tid3400__5415, __tid3400__4854

assign inst__5419 = inst_pyri; // asn inst__5419, inst_pyri

assign __tid3873__5420 = inst__5419[19:15]; // asn __tid3873__5420, %bit_read(inst__5419, 19<uint<6>>, 15<uint<5>>)

assign ra1__5422 = __tid3873__5420; // asn ra1__5422, __tid3873__5420

assign inst__5428 = inst_pyri; // asn inst__5428, inst_pyri

assign __tid3891__5429 = inst__5428[11:7]; // asn __tid3891__5429, %bit_read(inst__5428, 11<uint<5>>, 7<uint<4>>)

assign __tid3907__5447 = 0; // asn __tid3907__5447, false

assign inst__5454 = inst_pyri; // asn inst__5454, inst_pyri

assign __tid3941__5463 = inst__5454[11:7]; // asn __tid3941__5463, %bit_read(inst__5454, 11<uint<5>>, 7<uint<4>>)

assign __tid3954__5474 = __tid3873__5420; // asn __tid3954__5474, __tid3873__5420

assign __tid3958__5476 = __tid3954__5474 != 5'd0; // asn __tid3958__5476, %is_not_equal(__tid3954__5474, 0<uint<5>>)

assign __tid3958__5481 = 0; // asn __tid3958__5481, false

assign __tid3962__5486 = reg_addr_pyri; // asn __tid3962__5486, reg_addr_pyri

assign reg_addr__read__5487 = 1; // asn reg_addr__read__5487, true

assign __tid3964__5489 = __tid3873__5420; // asn __tid3964__5489, __tid3873__5420

assign __tid3966__5490 = __tid3962__5486 == { 59'd0, __tid3964__5489 }; // asn __tid3966__5490, %is_equal(__tid3962__5486, __tid3964__5489)

assign __tid3966__5497 = 0; // asn __tid3966__5497, false

assign reg_data__read__5505 = 1; // asn reg_data__read__5505, true

assign ra1__5507 = __tid3873__5420; // asn ra1__5507, __tid3873__5420

assign src1__5557 = 64'd0; // asn src1__5557, 0<uint<1>>

assign __tid3404__5564 = __tid3404__4856; // asn __tid3404__5564, __tid3404__4856

assign inst__5568 = inst_pyri; // asn inst__5568, inst_pyri

assign __tid4010__5569 = inst__5568[11:7]; // asn __tid4010__5569, %bit_read(inst__5568, 11<uint<5>>, 7<uint<4>>)

assign __tid3408__5599 = __tid3408__4858; // asn __tid3408__5599, __tid3408__4858

assign inst__5603 = inst_pyri; // asn inst__5603, inst_pyri

assign __tid4048__5604 = inst__5603[11:7]; // asn __tid4048__5604, %bit_read(inst__5603, 11<uint<5>>, 7<uint<4>>)

assign inst__5616 = inst_pyri; // asn inst__5616, inst_pyri

assign __tid4084__5626 = inst__5616[31:12]; // asn __tid4084__5626, %bit_read(inst__5616, 31<uint<6>>, 12<uint<5>>)

assign __tid4098__5632 = inst__5616[11:7]; // asn __tid4098__5632, %bit_read(inst__5616, 11<uint<5>>, 7<uint<4>>)

assign inst__keep__5638 = 0; // asn inst__keep__5638, false

assign src1__write__5659 = 0; // asn src1__write__5659, false

assign inst_pyro = inst_pyri; // asn inst_pyro, inst_pyri

assign pc_pyro = pc_pyri; // asn pc_pyro, pc_pyri

assign inst__read__6498 = inst_valid_pyri; // asn inst__read__6498, inst_valid_pyri

assign __tid5976__6518 = ! inst__read__6498; // asn __tid5976__6518, %logical_not(inst__read__6498)

assign __tid5978__6519 = __tid5976__6518 | inst_valid_pyri; // asn __tid5978__6519, %logical_or(__tid5976__6518, inst_valid_pyri)

assign __tid5998__6529 = ! inst_retry_pyri; // asn __tid5998__6529, %logical_not(inst_retry_pyri)

assign __tid6004__6532 = ! pc_retry_pyri; // asn __tid6004__6532, %logical_not(pc_retry_pyri)

assign __tid6010__6535 = ! src1_retry_pyri; // asn __tid6010__6535, %logical_not(src1_retry_pyri)

assign __tid6016__6538 = ! src2_retry_pyri; // asn __tid6016__6538, %logical_not(src2_retry_pyri)

assign raw_valid_pyro = 1; // asn raw_valid_pyro, true

assign data_valid_pyro = 1; // asn data_valid_pyro, true

assign __tid3152__17331 = ! __tid3152__4655; // asn __tid3152__17331, %logical_not(__tid3152__4655)

assign __tid16148__17355 = ! __tid3152__4655; // asn __tid16148__17355, %logical_not(__tid3152__4655)

assign __tid16152__17357 = (__tid16148__17355) ? reg_data_pyri : __tid16150__17356;

assign __tid16407__17361 = { 59'd0, ra1__4977 } == reg_addr__4664; // asn __tid16407__17361, %is_equal(ra1__4977, reg_addr__4664)

assign __tid16417__17364 = { 59'd0, ra1__5302 } == reg_addr__4664; // asn __tid16417__17364, %is_equal(ra1__5302, reg_addr__4664)

assign __tid16428__17367 = { 59'd0, ra1__5507 } == reg_addr__4664; // asn __tid16428__17367, %is_equal(ra1__5507, reg_addr__4664)

assign __tid16464__17370 = { 59'd0, ra2__5027 } == reg_addr__4664; // asn __tid16464__17370, %is_equal(ra2__5027, reg_addr__4664)

assign __tid3129__4641 = reg_addr_valid_pyri; // asn __tid3129__4641, reg_addr_valid_pyri

assign __tid3131__4646 = (__tid3129__4641) ? reg_data_valid_pyri : __tid3131__4645;

assign __tid3139__4647 = __tid3129__4641 & __tid3131__4646; // asn __tid3139__4647, %logical_and(__tid3129__4641, __tid3131__4646)

assign __tid3141__4648 = __tid3139__4647; // asn __tid3141__4648, __tid3139__4647

assign data__write__4704 = (__tid3141__4648) ? __tid3152__17331 : data__write__4599;

assign raw__4706 = (__tid3141__4648) ? raw__4686 : raw__4634;

assign raw__4874 = raw__4706; // asn raw__4874, raw__4706

assign __tid3440__4875 = raw__4874[ra1__4867]; // asn __tid3440__4875, %bit_read(raw__4874, ra1__4867)

assign raw__4877 = raw__4706; // asn raw__4877, raw__4706

assign __tid3446__4878 = raw__4877[ra2__4873]; // asn __tid3446__4878, %bit_read(raw__4877, ra2__4873)

assign __tid3448__4880 = __tid3440__4875; // asn __tid3448__4880, __tid3440__4875

assign __tid3452__4882 = __tid3448__4880 == 1'd1; // asn __tid3452__4882, %is_equal(__tid3448__4880, 1<uint<1>>)

assign __tid3462__4883 = ! __tid3452__4882; // asn __tid3462__4883, %logical_not(__tid3452__4882)

assign __tid3454__4886 = __tid3446__4878; // asn __tid3454__4886, __tid3446__4878

assign __tid3458__4888 = __tid3454__4886 == 1'd1; // asn __tid3458__4888, %is_equal(__tid3454__4886, 1<uint<1>>)

assign __tid3458__4894 = (__tid3462__4883) ? __tid3458__4888 : __tid3458__4893;

assign __tid3468__4895 = __tid3452__4882 | __tid3458__4894; // asn __tid3468__4895, %logical_or(__tid3452__4882, __tid3458__4894)

assign __tid3470__4896 = __tid3468__4895; // asn __tid3470__4896, __tid3468__4895

assign raw__4904 = raw__4706; // asn raw__4904, raw__4706

assign __tid3487__4905 = raw__4904[__tid3483__4902]; // asn __tid3487__4905, %bit_read(raw__4904, __tid3483__4902)

assign __tid3495__4913 = __tid3487__4905; // asn __tid3495__4913, __tid3487__4905

assign __tid3499__4915 = __tid3495__4913 == 1'd1; // asn __tid3499__4915, %is_equal(__tid3495__4913, 1<uint<1>>)

assign __tid3499__4921 = (__tid3493__4910) ? __tid3499__4915 : __tid3499__4920;

assign __tid3507__4922 = __tid3493__4910 & __tid3499__4921; // asn __tid3507__4922, %logical_and(__tid3493__4910, __tid3499__4921)

assign __tid3509__4923 = __tid3507__4922; // asn __tid3509__4923, __tid3507__4922

assign __tid3511__4924 = ! __tid3470__4896; // asn __tid3511__4924, %logical_not(__tid3470__4896)

assign __tid3513__4925 = __tid3511__4924 & __tid3509__4923; // asn __tid3513__4925, %logical_and(__tid3511__4924, __tid3509__4923)

assign __tid3513__4928 = __tid3513__4925; // asn __tid3513__4928, __tid3513__4925

assign __tid3536__4941 = reg_addr_valid_pyri; // asn __tid3536__4941, reg_addr_valid_pyri

assign __tid3542__4952 = (__tid3536__4941) ? __tid3542__4946 : __tid3542__4951;

assign __tid3544__4953 = __tid3536__4941 & __tid3542__4952; // asn __tid3544__4953, %logical_and(__tid3536__4941, __tid3542__4952)

assign reg_addr__read__4964 = (__tid3544__4953) ? reg_addr__read__4957 : __tid3139__4647;

assign __tid3550__4968 = (__tid3544__4953) ? __tid3550__4960 : __tid3550__4967;

assign __tid3562__4969 = __tid3544__4953 & __tid3550__4968; // asn __tid3562__4969, %logical_and(__tid3544__4953, __tid3550__4968)

assign __tid3564__4970 = __tid3562__4969; // asn __tid3564__4970, __tid3562__4969

assign reg_data__read__4985 = (__tid3564__4970) ? reg_data__read__4975 : __tid3139__4647;

assign __tid3590__4992 = reg_addr_valid_pyri; // asn __tid3590__4992, reg_addr_valid_pyri

assign __tid3596__5003 = (__tid3590__4992) ? __tid3596__4997 : __tid3596__5002;

assign __tid3598__5004 = __tid3590__4992 & __tid3596__5003; // asn __tid3598__5004, %logical_and(__tid3590__4992, __tid3596__5003)

assign reg_addr__read__5014 = (__tid3598__5004) ? reg_addr__read__5008 : reg_addr__read__4964;

assign __tid3604__5018 = (__tid3598__5004) ? __tid3604__5011 : __tid3604__5017;

assign __tid3616__5019 = __tid3598__5004 & __tid3604__5018; // asn __tid3616__5019, %logical_and(__tid3598__5004, __tid3604__5018)

assign __tid3618__5020 = __tid3616__5019; // asn __tid3618__5020, __tid3616__5019

assign reg_data__read__5034 = (__tid3618__5020) ? reg_data__read__5025 : reg_data__read__4985;

assign raw__5067 = raw__4706; // asn raw__5067, raw__4706

assign raw__5068 = (raw__5067 & ~(32'd0 | (1 << __tid3678__5065[4:0]))) | ((32'd1 & 1) << __tid3678__5065[4:0]);

assign raw__5076 = (__tid3667__5060) ? raw__5068 : raw__4706;

assign reg_addr__read__5103 = (__tid3513__4928) ? __tid3139__4647 : reg_addr__read__5014;

assign reg_data__read__5114 = (__tid3513__4928) ? __tid3139__4647 : reg_data__read__5034;

assign raw__5159 = (__tid3513__4928) ? raw__4706 : raw__5076;

assign inst__keep__5162 = (__tid3470__4896) ? inst__keep__4927 : __tid3513__4925;

assign reg_addr__read__5176 = (__tid3470__4896) ? __tid3139__4647 : reg_addr__read__5103;

assign reg_data__read__5184 = (__tid3470__4896) ? __tid3139__4647 : reg_data__read__5114;

assign raw__5215 = (__tid3470__4896) ? raw__4706 : raw__5159;

assign raw__5226 = raw__4706; // asn raw__5226, raw__4706

assign __tid3707__5227 = raw__5226[ra1__5225]; // asn __tid3707__5227, %bit_read(raw__5226, ra1__5225)

assign raw__5234 = raw__4706; // asn raw__5234, raw__4706

assign __tid3720__5235 = raw__5234[__tid3716__5232]; // asn __tid3720__5235, %bit_read(raw__5234, __tid3716__5232)

assign __tid3722__5237 = __tid3707__5227; // asn __tid3722__5237, __tid3707__5227

assign __tid3726__5239 = __tid3722__5237 == 1'd1; // asn __tid3726__5239, %is_equal(__tid3722__5237, 1<uint<1>>)

assign __tid3736__5240 = ! __tid3726__5239; // asn __tid3736__5240, %logical_not(__tid3726__5239)

assign __tid3728__5243 = __tid3720__5235; // asn __tid3728__5243, __tid3720__5235

assign __tid3732__5245 = __tid3728__5243 == 1'd1; // asn __tid3732__5245, %is_equal(__tid3728__5243, 1<uint<1>>)

assign __tid3732__5251 = (__tid3736__5240) ? __tid3732__5245 : __tid3732__5250;

assign __tid3742__5252 = __tid3726__5239 | __tid3732__5251; // asn __tid3742__5252, %logical_or(__tid3726__5239, __tid3732__5251)

assign __tid3744__5253 = __tid3742__5252; // asn __tid3744__5253, __tid3742__5252

assign __tid3764__5266 = reg_addr_valid_pyri; // asn __tid3764__5266, reg_addr_valid_pyri

assign __tid3770__5277 = (__tid3764__5266) ? __tid3770__5271 : __tid3770__5276;

assign __tid3772__5278 = __tid3764__5266 & __tid3770__5277; // asn __tid3772__5278, %logical_and(__tid3764__5266, __tid3770__5277)

assign reg_addr__read__5289 = (__tid3772__5278) ? reg_addr__read__5282 : __tid3139__4647;

assign __tid3778__5293 = (__tid3772__5278) ? __tid3778__5285 : __tid3778__5292;

assign __tid3790__5294 = __tid3772__5278 & __tid3778__5293; // asn __tid3790__5294, %logical_and(__tid3772__5278, __tid3778__5293)

assign __tid3792__5295 = __tid3790__5294; // asn __tid3792__5295, __tid3790__5294

assign reg_data__read__5310 = (__tid3792__5295) ? reg_data__read__5300 : __tid3139__4647;

assign raw__5344 = raw__4706; // asn raw__5344, raw__4706

assign raw__5345 = (raw__5344 & ~(32'd0 | (1 << __tid3859__5342[4:0]))) | ((32'd1 & 1) << __tid3859__5342[4:0]);

assign reg_addr__read__5369 = (__tid3744__5253) ? __tid3139__4647 : reg_addr__read__5289;

assign reg_data__read__5381 = (__tid3744__5253) ? __tid3139__4647 : reg_data__read__5310;

assign src2__5389 = (__tid3744__5253) ? src2__5388 : src2__5337;

assign raw__5411 = (__tid3744__5253) ? raw__4706 : raw__5345;

assign raw__5423 = raw__4706; // asn raw__5423, raw__4706

assign __tid3882__5424 = raw__5423[ra1__5422]; // asn __tid3882__5424, %bit_read(raw__5423, ra1__5422)

assign raw__5431 = raw__4706; // asn raw__5431, raw__4706

assign __tid3895__5432 = raw__5431[__tid3891__5429]; // asn __tid3895__5432, %bit_read(raw__5431, __tid3891__5429)

assign __tid3897__5434 = __tid3882__5424; // asn __tid3897__5434, __tid3882__5424

assign __tid3901__5436 = __tid3897__5434 == 1'd1; // asn __tid3901__5436, %is_equal(__tid3897__5434, 1<uint<1>>)

assign __tid3911__5437 = ! __tid3901__5436; // asn __tid3911__5437, %logical_not(__tid3901__5436)

assign __tid3903__5440 = __tid3895__5432; // asn __tid3903__5440, __tid3895__5432

assign __tid3907__5442 = __tid3903__5440 == 1'd1; // asn __tid3907__5442, %is_equal(__tid3903__5440, 1<uint<1>>)

assign __tid3907__5448 = (__tid3911__5437) ? __tid3907__5442 : __tid3907__5447;

assign __tid3917__5449 = __tid3901__5436 | __tid3907__5448; // asn __tid3917__5449, %logical_or(__tid3901__5436, __tid3907__5448)

assign __tid3919__5450 = __tid3917__5449; // asn __tid3919__5450, __tid3917__5449

assign raw__5465 = raw__4706; // asn raw__5465, raw__4706

assign raw__5466 = (raw__5465 & ~(32'd0 | (1 << __tid3941__5463[4:0]))) | ((32'd1 & 1) << __tid3941__5463[4:0]);

assign __tid3952__5471 = reg_addr_valid_pyri; // asn __tid3952__5471, reg_addr_valid_pyri

assign __tid3958__5482 = (__tid3952__5471) ? __tid3958__5476 : __tid3958__5481;

assign __tid3960__5483 = __tid3952__5471 & __tid3958__5482; // asn __tid3960__5483, %logical_and(__tid3952__5471, __tid3958__5482)

assign reg_addr__read__5494 = (__tid3960__5483) ? reg_addr__read__5487 : __tid3139__4647;

assign __tid3966__5498 = (__tid3960__5483) ? __tid3966__5490 : __tid3966__5497;

assign __tid3978__5499 = __tid3960__5483 & __tid3966__5498; // asn __tid3978__5499, %logical_and(__tid3960__5483, __tid3966__5498)

assign __tid3980__5500 = __tid3978__5499; // asn __tid3980__5500, __tid3978__5499

assign reg_data__read__5515 = (__tid3980__5500) ? reg_data__read__5505 : __tid3139__4647;

assign raw__5535 = (__tid3919__5450) ? raw__4706 : raw__5466;

assign reg_addr__read__5549 = (__tid3919__5450) ? __tid3139__4647 : reg_addr__read__5494;

assign reg_data__read__5561 = (__tid3919__5450) ? __tid3139__4647 : reg_data__read__5515;

assign raw__5571 = raw__4706; // asn raw__5571, raw__4706

assign __tid4014__5572 = raw__5571[__tid4010__5569]; // asn __tid4014__5572, %bit_read(raw__5571, __tid4010__5569)

assign __tid4016__5574 = __tid4014__5572; // asn __tid4016__5574, __tid4014__5572

assign __tid4022__5576 = __tid4016__5574 == 1'd1; // asn __tid4022__5576, %is_equal(__tid4016__5574, 1<uint<1>>)

assign __tid4024__5577 = __tid4022__5576; // asn __tid4024__5577, __tid4022__5576

assign raw__5606 = raw__4706; // asn raw__5606, raw__4706

assign __tid4052__5607 = raw__5606[__tid4048__5604]; // asn __tid4052__5607, %bit_read(raw__5606, __tid4048__5604)

assign __tid4054__5609 = __tid4052__5607; // asn __tid4054__5609, __tid4052__5607

assign __tid4060__5611 = __tid4054__5609 == 1'd1; // asn __tid4060__5611, %is_equal(__tid4054__5609, 1<uint<1>>)

assign __tid4062__5612 = __tid4060__5611; // asn __tid4062__5612, __tid4060__5611

assign raw__5634 = raw__4706; // asn raw__5634, raw__4706

assign raw__5635 = (raw__5634 & ~(32'd0 | (1 << __tid4098__5632[4:0]))) | ((32'd1 & 1) << __tid4098__5632[4:0]);

assign src1__5658 = (__tid4062__5612) ? src1__5557 : { 44'd0, __tid4084__5626 };

assign raw__5665 = (__tid4062__5612) ? raw__4706 : raw__5635;

assign inst__keep__5706 = (__tid3408__5599) ? __tid4060__5611 : inst__keep__5638;

assign src1__5716 = (__tid3408__5599) ? src1__5658 : src1__5557;

assign raw__5720 = (__tid3408__5599) ? raw__5665 : raw__4706;

assign inst__keep__5738 = (__tid3404__5564) ? __tid4022__5576 : inst__keep__5706;

assign src1__5755 = (__tid3404__5564) ? src1__5557 : src1__5716;

assign raw__5760 = (__tid3404__5564) ? raw__4706 : raw__5720;

assign inst__keep__5793 = (__tid3400__5415) ? __tid3917__5449 : inst__keep__5738;

assign raw__5801 = (__tid3400__5415) ? raw__5535 : raw__5760;

assign reg_addr__read__5810 = (__tid3400__5415) ? reg_addr__read__5549 : __tid3139__4647;

assign reg_data__read__5817 = (__tid3400__5415) ? reg_data__read__5561 : __tid3139__4647;

assign inst__keep__5871 = (__tid3396__5218) ? __tid3742__5252 : inst__keep__5793;

assign reg_addr__read__5884 = (__tid3396__5218) ? reg_addr__read__5369 : reg_addr__read__5810;

assign reg_data__read__5891 = (__tid3396__5218) ? reg_data__read__5381 : reg_data__read__5817;

assign src2__5896 = (__tid3396__5218) ? src2__5389 : src2__5388;

assign raw__5908 = (__tid3396__5218) ? raw__5411 : raw__5801;

assign inst__keep__6017 = (__tid3268__4772) ? inst__keep__5162 : inst__keep__5871;

assign reg_addr__read__6030 = (__tid3268__4772) ? reg_addr__read__5176 : reg_addr__read__5884;

assign reg_data__read__6037 = (__tid3268__4772) ? reg_data__read__5184 : reg_data__read__5891;

assign raw__6072 = (__tid3268__4772) ? raw__5215 : raw__5908;

assign raw__6314 = (__tid3189__4711) ? raw__6072 : raw__4706;

assign inst__keep__6339 = (__tid3189__4711) ? inst__keep__6017 : inst__keep__4594;

assign reg_addr__read__6351 = (__tid3189__4711) ? reg_addr__read__6030 : __tid3139__4647;

assign reg_data__read__6358 = (__tid3189__4711) ? reg_data__read__6037 : __tid3139__4647;

assign inst__keep__6489 = inst__keep__6339; // asn inst__keep__6489, inst__keep__6339

assign data__write__6494 = data__write__4704; // asn data__write__6494, data__write__4704

assign reg_data__read__6500 = reg_data__read__6358; // asn reg_data__read__6500, reg_data__read__6358

assign reg_addr__read__6501 = reg_addr__read__6351; // asn reg_addr__read__6501, reg_addr__read__6351

assign __tid5968__6514 = ! reg_addr__read__6501; // asn __tid5968__6514, %logical_not(reg_addr__read__6501)

assign __tid5970__6515 = __tid5968__6514 | reg_addr_valid_pyri; // asn __tid5970__6515, %logical_or(__tid5968__6514, reg_addr_valid_pyri)

assign __tid5972__6516 = ! reg_data__read__6500; // asn __tid5972__6516, %logical_not(reg_data__read__6500)

assign __tid5974__6517 = __tid5972__6516 | reg_data_valid_pyri; // asn __tid5974__6517, %logical_or(__tid5972__6516, reg_data_valid_pyri)

assign __tid6021__6540 = __tid5970__6515 & __tid5974__6517; // asn __tid6021__6540, %logical_and(__tid5970__6515, __tid5974__6517)

assign __tid6023__6541 = __tid6021__6540 & __tid5978__6519; // asn __tid6023__6541, %logical_and(__tid6021__6540, __tid5978__6519)

assign __tid3513__17332 = ! __tid3513__4928; // asn __tid3513__17332, %logical_not(__tid3513__4928)

assign __tid3513__17333 = ! __tid3513__4928; // asn __tid3513__17333, %logical_not(__tid3513__4928)

assign __tid3513__17334 = ! __tid3513__4928; // asn __tid3513__17334, %logical_not(__tid3513__4928)

assign __tid3513__17335 = ! __tid3513__4928; // asn __tid3513__17335, %logical_not(__tid3513__4928)

assign __tid3513__17336 = ! __tid3513__4928; // asn __tid3513__17336, %logical_not(__tid3513__4928)

assign __tid3744__17337 = ! __tid3744__5253; // asn __tid3744__17337, %logical_not(__tid3744__5253)

assign __tid3744__17338 = ! __tid3744__5253; // asn __tid3744__17338, %logical_not(__tid3744__5253)

assign __tid3744__17339 = ! __tid3744__5253; // asn __tid3744__17339, %logical_not(__tid3744__5253)

assign __tid3744__17340 = ! __tid3744__5253; // asn __tid3744__17340, %logical_not(__tid3744__5253)

assign __tid3744__17341 = ! __tid3744__5253; // asn __tid3744__17341, %logical_not(__tid3744__5253)

assign __tid3919__17342 = ! __tid3919__5450; // asn __tid3919__17342, %logical_not(__tid3919__5450)

assign __tid3919__17343 = ! __tid3919__5450; // asn __tid3919__17343, %logical_not(__tid3919__5450)

assign __tid3919__17344 = ! __tid3919__5450; // asn __tid3919__17344, %logical_not(__tid3919__5450)

assign __tid3919__17345 = ! __tid3919__5450; // asn __tid3919__17345, %logical_not(__tid3919__5450)

assign __tid4024__17346 = ! __tid4024__5577; // asn __tid4024__17346, %logical_not(__tid4024__5577)

assign __tid4024__17347 = ! __tid4024__5577; // asn __tid4024__17347, %logical_not(__tid4024__5577)

assign __tid4024__17348 = ! __tid4024__5577; // asn __tid4024__17348, %logical_not(__tid4024__5577)

assign __tid4062__17349 = ! __tid4062__5612; // asn __tid4062__17349, %logical_not(__tid4062__5612)

assign __tid4062__17350 = ! __tid4062__5612; // asn __tid4062__17350, %logical_not(__tid4062__5612)

assign __tid4062__17351 = ! __tid4062__5612; // asn __tid4062__17351, %logical_not(__tid4062__5612)

assign __tid4062__17352 = ! __tid4062__5612; // asn __tid4062__17352, %logical_not(__tid4062__5612)

assign __tid16172__17359 = (__tid3141__4648) ? __tid16152__17357 : __tid16170__17358;

assign __tid16409__17362 = (__tid16407__17361) ? __tid16172__17359 : __tid16405__17360;

assign __tid16419__17365 = (__tid16417__17364) ? __tid16172__17359 : __tid16415__17363;

assign __tid16430__17368 = (__tid16428__17367) ? __tid16172__17359 : __tid16426__17366;

assign __tid16466__17371 = (__tid16464__17370) ? __tid16172__17359 : __tid16462__17369;

assign src1__4982 = (__tid3564__4970) ? reg_data_pyri : __tid16409__17362;

assign src2__5032 = (__tid3618__5020) ? reg_data_pyri : __tid16466__17371;

assign inst__write__5164 = (__tid3470__4896) ? inst__write__5084 : __tid3513__17332;

assign pc__write__5167 = (__tid3470__4896) ? pc__write__5089 : __tid3513__17333;

assign pc__read__5168 = (__tid3470__4896) ? pc__read__5091 : __tid3513__17334;

assign src1__write__5183 = (__tid3470__4896) ? src1__write__5112 : __tid3513__17335;

assign src2__write__5200 = (__tid3470__4896) ? src2__write__5135 : __tid3513__17336;

assign src1__5307 = (__tid3792__5295) ? reg_data_pyri : __tid16419__17365;

assign src1__5378 = (__tid3744__5253) ? src1__5377 : src1__5307;

assign src1__5512 = (__tid3980__5500) ? reg_data_pyri : __tid16430__17368;

assign src1__5558 = (__tid3919__5450) ? src1__5377 : src1__5512;

assign inst__write__5708 = (__tid3408__5599) ? __tid4062__17349 : __tid3412__4860;

assign pc__write__5710 = (__tid3408__5599) ? __tid4062__17350 : __tid3412__4860;

assign pc__read__5711 = (__tid3408__5599) ? __tid4062__17351 : __tid3412__4860;

assign src1__write__5717 = (__tid3408__5599) ? __tid4062__17352 : src1__write__5659;

assign inst__write__5740 = (__tid3404__5564) ? __tid4024__17346 : inst__write__5708;

assign pc__write__5742 = (__tid3404__5564) ? __tid4024__17347 : pc__write__5710;

assign pc__read__5743 = (__tid3404__5564) ? __tid4024__17348 : pc__read__5711;

assign src1__write__5756 = (__tid3404__5564) ? src1__write__5659 : src1__write__5717;

assign inst__write__5795 = (__tid3400__5415) ? __tid3919__17342 : inst__write__5740;

assign pc__write__5797 = (__tid3400__5415) ? __tid3919__17343 : pc__write__5742;

assign pc__read__5798 = (__tid3400__5415) ? __tid3919__17344 : pc__read__5743;

assign src1__5815 = (__tid3400__5415) ? src1__5558 : src1__5755;

assign src1__write__5816 = (__tid3400__5415) ? __tid3919__17345 : src1__write__5756;

assign inst__write__5873 = (__tid3396__5218) ? __tid3744__17337 : inst__write__5795;

assign pc__write__5875 = (__tid3396__5218) ? __tid3744__17338 : pc__write__5797;

assign pc__read__5876 = (__tid3396__5218) ? __tid3744__17339 : pc__read__5798;

assign src1__5889 = (__tid3396__5218) ? src1__5378 : src1__5815;

assign src1__write__5890 = (__tid3396__5218) ? __tid3744__17340 : src1__write__5816;

assign src2__write__5897 = (__tid3396__5218) ? __tid3744__17341 : src2__write__5390;

assign inst__write__6019 = (__tid3268__4772) ? inst__write__5164 : inst__write__5873;

assign pc__write__6021 = (__tid3268__4772) ? pc__write__5167 : pc__write__5875;

assign pc__read__6022 = (__tid3268__4772) ? pc__read__5168 : pc__read__5876;

assign src1__6035 = (__tid3268__4772) ? src1__4982 : src1__5889;

assign src1__write__6036 = (__tid3268__4772) ? src1__write__5183 : src1__write__5890;

assign src2__6054 = (__tid3268__4772) ? src2__5032 : src2__5896;

assign src2__write__6055 = (__tid3268__4772) ? src2__write__5200 : src2__write__5897;

assign inst__write__6340 = (__tid3189__4711) ? inst__write__6019 : inst__write__4598;

assign pc__write__6342 = (__tid3189__4711) ? pc__write__6021 : pc__write__4597;

assign pc__read__6343 = (__tid3189__4711) ? pc__read__6022 : pc__read__4602;

assign src1_pyro = src1__6035; // asn src1_pyro, src1__6035

assign src1__write__6357 = (__tid3189__4711) ? src1__write__6036 : src1__write__4596;

assign src2_pyro = src2__6054; // asn src2_pyro, src2__6054

assign src2__write__6376 = (__tid3189__4711) ? src2__write__6055 : src2__write__4595;

assign src2__write__6490 = src2__write__6376; // asn src2__write__6490, src2__write__6376

assign src1__write__6491 = src1__write__6357; // asn src1__write__6491, src1__write__6357

assign pc__write__6492 = pc__write__6342; // asn pc__write__6492, pc__write__6342

assign inst__write__6493 = inst__write__6340; // asn inst__write__6493, inst__write__6340

assign pc__read__6497 = pc__read__6343; // asn pc__read__6497, pc__read__6343

assign __tid5980__6520 = ! pc__read__6497; // asn __tid5980__6520, %logical_not(pc__read__6497)

assign __tid5982__6521 = __tid5980__6520 | pc_valid_pyri; // asn __tid5982__6521, %logical_or(__tid5980__6520, pc_valid_pyri)

assign __tid5996__6528 = ! inst__write__6493; // asn __tid5996__6528, %logical_not(inst__write__6493)

assign __tid6000__6530 = __tid5996__6528 | __tid5998__6529; // asn __tid6000__6530, %logical_or(__tid5996__6528, __tid5998__6529)

assign __tid6002__6531 = ! pc__write__6492; // asn __tid6002__6531, %logical_not(pc__write__6492)

assign __tid6006__6533 = __tid6002__6531 | __tid6004__6532; // asn __tid6006__6533, %logical_or(__tid6002__6531, __tid6004__6532)

assign __tid6008__6534 = ! src1__write__6491; // asn __tid6008__6534, %logical_not(src1__write__6491)

assign __tid6012__6536 = __tid6008__6534 | __tid6010__6535; // asn __tid6012__6536, %logical_or(__tid6008__6534, __tid6010__6535)

assign __tid6014__6537 = ! src2__write__6490; // asn __tid6014__6537, %logical_not(src2__write__6490)

assign __tid6018__6539 = __tid6014__6537 | __tid6016__6538; // asn __tid6018__6539, %logical_or(__tid6014__6537, __tid6016__6538)

assign __tid6025__6542 = __tid6023__6541 & __tid5982__6521; // asn __tid6025__6542, %logical_and(__tid6023__6541, __tid5982__6521)

assign __tid6029__6544 = __tid6025__6542; // asn __tid6029__6544, __tid6025__6542

assign __tid6031__6545 = __tid6029__6544 & __tid6000__6530; // asn __tid6031__6545, %logical_and(__tid6029__6544, __tid6000__6530)

assign __tid6033__6546 = __tid6031__6545 & __tid6006__6533; // asn __tid6033__6546, %logical_and(__tid6031__6545, __tid6006__6533)

assign __tid6035__6547 = __tid6033__6546 & __tid6012__6536; // asn __tid6035__6547, %logical_and(__tid6033__6546, __tid6012__6536)

assign __tid6037__6548 = __tid6035__6547 & __tid6018__6539; // asn __tid6037__6548, %logical_and(__tid6035__6547, __tid6018__6539)

assign __block_valid__6549 = __tid6037__6548; // asn __block_valid__6549, __tid6037__6548

assign __tid6045__6560 = ! __block_valid__6549; // asn __tid6045__6560, %logical_not(__block_valid__6549)

assign __tid6047__6561 = __tid6037__6548; // asn __tid6047__6561, __tid6037__6548

assign raw_pyro = (__tid6047__6561) ? raw__6314 : raw;

assign __tid6052__6564 = __block_valid__6549 & data__write__6494; // asn __tid6052__6564, %logical_and(__block_valid__6549, data__write__6494)

assign inst_valid_pyro = __block_valid__6549 & inst__write__6493; // asn inst_valid_pyro, %logical_and(__block_valid__6549, inst__write__6493)

assign pc_valid_pyro = __block_valid__6549 & pc__write__6492; // asn pc_valid_pyro, %logical_and(__block_valid__6549, pc__write__6492)

assign src1_valid_pyro = __block_valid__6549 & src1__write__6491; // asn src1_valid_pyro, %logical_and(__block_valid__6549, src1__write__6491)

assign src2_valid_pyro = __block_valid__6549 & src2__write__6490; // asn src2_valid_pyro, %logical_and(__block_valid__6549, src2__write__6490)

assign __tid6065__6571 = __tid6045__6560 | __tid5968__6514; // asn __tid6065__6571, %logical_or(__tid6045__6560, __tid5968__6514)

assign reg_addr_retry_pyro = reg_addr_valid_pyri & __tid6065__6571; // asn reg_addr_retry_pyro, %logical_and(reg_addr_valid_pyri, __tid6065__6571)

assign __tid6069__6573 = __tid6045__6560 | __tid5972__6516; // asn __tid6069__6573, %logical_or(__tid6045__6560, __tid5972__6516)

assign reg_data_retry_pyro = reg_data_valid_pyri & __tid6069__6573; // asn reg_data_retry_pyro, %logical_and(reg_data_valid_pyri, __tid6069__6573)

assign __tid6073__6575 = __tid6045__6560 | __tid5976__6518; // asn __tid6073__6575, %logical_or(__tid6045__6560, __tid5976__6518)

assign __tid6075__6576 = __tid6073__6575 | inst__keep__6489; // asn __tid6075__6576, %logical_or(__tid6073__6575, inst__keep__6489)

assign inst_retry_pyro = inst_valid_pyri & __tid6075__6576; // asn inst_retry_pyro, %logical_and(inst_valid_pyri, __tid6075__6576)

assign __tid6079__6578 = __tid6045__6560 | __tid5980__6520; // asn __tid6079__6578, %logical_or(__tid6045__6560, __tid5980__6520)

assign pc_retry_pyro = pc_valid_pyri & __tid6079__6578; // asn pc_retry_pyro, %logical_and(pc_valid_pyri, __tid6079__6578)

assign __tid16487__17373 = (__tid6052__6564) ? __tid16172__17359 : __tid16485__17372;

assign __tid17235 = __tid16487__17373; // asn __tid17235, __tid16487__17373

decode_block_data decode_block_data_inst(.clk(clk), .__tid16150__17356(__tid16150__17356), .reg_addr__4664(reg_addr__4664), .__tid16170__17358(__tid16170__17358), .__tid16405__17360(__tid16405__17360), .ra1__4977(ra1__4977), .__tid16415__17363(__tid16415__17363), .ra1__5302(ra1__5302), .__tid16426__17366(__tid16426__17366), .ra1__5507(ra1__5507), .__tid16462__17369(__tid16462__17369), .ra2__5027(ra2__5027), .__tid16485__17372(__tid16485__17372), .__tid17235(__tid17235));





  always @(posedge clk) begin
    
      raw <= raw_pyro; // asn raw, raw_pyro
    
      raw <= raw_pyro; // asn raw, raw_pyro
    
  end




endmodule

