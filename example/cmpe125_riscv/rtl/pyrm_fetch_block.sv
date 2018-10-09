module pyrm_fetch_block(
	reset_pyri,
	branch_pc_pyri,
	branch_pc_valid_pyri,
	branch_pc_retry_pyro,
	pc_pyro,
	pc_valid_pyro,
	pc_retry_pyri,
	inst_pyro,
	inst_valid_pyro,
	inst_retry_pyri,
	clk
);


  

  

  

  


input  reset_pyri;
input [64-1:0] branch_pc_pyri;
input branch_pc_valid_pyri;
output branch_pc_retry_pyro;
output[64-1:0] pc_pyro;
output pc_valid_pyro;
input pc_retry_pyri;
output[32-1:0] inst_pyro;
output inst_valid_pyro;
input inst_retry_pyri;
input clk;
logic inst__write__2710;
logic pc__write__2714;
logic branch_pc__read__2715;
logic [64-1:0] internal_pc__2721;
logic internal_pc__write__2722;
logic [2-1:0] __tid2090__2724;
logic state__write__2726;
logic [2-1:0] __tid2132__2746;
logic __tid2138__2749;
logic __tid2140__2750;
logic [2-1:0] __tid2146__2754;
logic __tid2152__2757;
logic __tid2154__2758;
logic __tid2156__2759;
logic __tid2158__2760;
logic [64-1:0] __tid2165__2765;
logic [64-1:0] __tid2169__2768;
logic [64-1:0] __tid2175__2770;
logic [32-1:0] __tid2177__2772;
logic pc__write__2777;
logic [64-1:0] internal_pc__2783;
logic [64-1:0] internal_pc__2784;
logic internal_pc__write__2785;
logic sent_inst__2787;
logic __tid2158__2788;
logic __tid2205__2792;
logic [64-1:0] internal_pc__2795;
logic [2-1:0] __tid2212__2799;
logic [64-1:0] __tid2230__2810;
logic [64-1:0] __tid2234__2813;
logic [64-1:0] __tid2240__2815;
logic [32-1:0] __tid2242__2817;
logic [64-1:0] internal_pc__2820;
logic [64-1:0] internal_pc__2825;
logic internal_pc__write__2826;
logic branch_pc__read__2828;
logic [2-1:0] state__2833;
logic state__write__2834;
logic pc__write__2838;
logic [32-1:0] inst_out__2862;
logic [32-1:0] inst_out__2863;
logic sent_inst__2864;
logic [64-1:0] internal_pc__2870;
logic internal_pc__write__2871;
logic branch_pc__read__2872;
logic [2-1:0] state__2874;
logic state__write__2875;
logic pc__write__2877;
logic [32-1:0] inst_out__2889;
logic sent_inst__2890;
logic [32-1:0] inst_out__2907;
logic [64-1:0] pc__2908;
logic pc__write__2909;
logic [64-1:0] internal_pc__2912;
logic internal_pc__write__2913;
logic sent_inst__2914;
logic branch_pc__read__2917;
logic [2-1:0] state__2919;
logic state__write__2920;
logic __tid2267__2928;
logic [32-1:0] inst_out__2932;
logic [7-1:0] __tid2275__2933;
logic [7-1:0] __tid2292__2942;
logic __tid2296__2944;
logic __tid2306__2945;
logic [7-1:0] __tid2298__2948;
logic __tid2302__2951;
logic __tid2302__2956;
logic __tid2302__2957;
logic __tid2312__2958;
logic __tid2314__2959;
logic [7-1:0] __tid2320__2962;
logic __tid2326__2964;
logic __tid2328__2965;
logic __tid2330__2966;
logic __tid2332__2967;
logic [2-1:0] __tid2335__2970;
logic __tid2332__2973;
logic [32-1:0] fpx2667__curr_inst__2976;
logic [5-1:0] __tid214__2977;
logic __tid216__2978;
logic [8-1:0] __tid229__2982;
logic [5-1:0] __tid236__2983;
logic __tid238__2984;
logic [10-1:0] __tid251__2988;
logic [9-1:0] __tid257__2989;
logic [10-1:0] __tid259__2990;
logic [20-1:0] __tid261__2991;
logic [21-1:0] __tid263__2992;
logic [64-1:0] fpx2667__imm__2993;
logic [6-1:0] __tid271__2994;
logic __tid273__2995;
logic __tid275__2996;
logic __tid281__2998;
logic __tid283__2999;
logic [64-1:0] fpx2667__imm__3003;
logic [64-1:0] fpx2667__imm__3004;
logic [64-1:0] fpx2667__imm__3007;
logic [64-1:0] __tid297__3008;
logic [64-1:0] __tid2348__3010;
logic [64-1:0] internal_pc__3011;
logic [64-1:0] internal_pc__3012;
logic internal_pc__write__3013;
logic [2-1:0] __tid2352__3016;
logic [2-1:0] __tid2360__3020;
logic [64-1:0] internal_pc__3078;
logic internal_pc__write__3080;
logic [2-1:0] state__3085;
logic [2-1:0] state__3091;
logic state__write__3092;
logic [64-1:0] internal_pc__3121;
logic internal_pc__write__3122;
logic [2-1:0] state__3170;
logic state__write__3171;
logic [64-1:0] internal_pc__3200;
logic internal_pc__write__3201;
logic [64-1:0] internal_pc__3206;
logic internal_pc__write__3207;
logic [2-1:0] state__3210;
logic state__write__3211;
logic pc__write__3251;
logic branch_pc__read__3256;
logic inst__write__3271;
logic inst__write__3327;
logic state__write__3328;
logic internal_pc__write__3329;
logic pc__write__3331;
logic branch_pc__read__3332;
logic __tid2995__3341;
logic __tid2997__3342;
logic __tid2999__3343;
logic __tid3001__3344;
logic __tid3003__3345;
logic __tid3023__3355;
logic __tid3025__3356;
logic __tid3027__3357;
logic __tid3030__3358;
logic __tid3036__3361;
logic __tid3038__3362;
logic __block_valid__3363;
logic __tid3045__3370;
logic __tid3051__3373;
logic __tid3056__3376;
logic __tid3063__3380;
logic reset_pyri;
logic reset_retry_pyro;
logic reset_valid_pyri;
logic [64-1:0] branch_pc_pyri;
logic branch_pc_retry_pyro;
logic branch_pc_valid_pyri;
logic [2-1:0] state_pyri;
logic [64-1:0] internal_pc_pyri;
logic [32-1:0] icache_pyri;
logic [64-1:0] pc_pyro;
logic pc_valid_pyro;
logic pc_retry_pyri;
logic [32-1:0] inst_pyro;
logic inst_valid_pyro;
logic inst_retry_pyri;
logic [2-1:0] state_pyro;
logic state_valid_pyro;
logic [64-1:0] internal_pc_pyro;
logic internal_pc_valid_pyro;
logic [32-1:0] icache_pyro;
logic icache_valid_pyro;



  
    logic[1:0] state /*verilator public*/;
  

  
    logic[63:0] internal_pc /*verilator public*/;
  

  
    logic[31:0] icache[4096-1:0];
  



assign inst__write__2710 = 0; // asn inst__write__2710, false

assign pc__write__2714 = 0; // asn pc__write__2714, false

assign branch_pc__read__2715 = 0; // asn branch_pc__read__2715, false

assign internal_pc__2721 = 64'd2147483648; // asn internal_pc__2721, 2147483648<uint<32>>

assign internal_pc__write__2722 = 1; // asn internal_pc__write__2722, true

assign __tid2090__2724 = 2'd1; // asn __tid2090__2724, 1<uint<2>>

assign state__write__2726 = 1; // asn state__write__2726, true

assign __tid2132__2746 = state; // asn __tid2132__2746, state

assign __tid2138__2749 = __tid2132__2746 == 2'd1; // asn __tid2138__2749, %is_equal(__tid2132__2746, 1<uint<2>>)

assign __tid2140__2750 = __tid2138__2749; // asn __tid2140__2750, __tid2138__2749

assign __tid2146__2754 = state; // asn __tid2146__2754, state

assign __tid2152__2757 = __tid2146__2754 == 2'd2; // asn __tid2152__2757, %is_equal(__tid2146__2754, 2<uint<2>>)

assign __tid2154__2758 = __tid2152__2757; // asn __tid2154__2758, __tid2152__2757

assign __tid2156__2759 = ! __tid2140__2750; // asn __tid2156__2759, %logical_not(__tid2140__2750)

assign __tid2158__2760 = __tid2156__2759 & __tid2154__2758; // asn __tid2158__2760, %logical_and(__tid2156__2759, __tid2154__2758)

assign __tid2165__2765 = internal_pc; // asn __tid2165__2765, internal_pc

assign __tid2169__2768 = __tid2165__2765 & 64'd65535; // asn __tid2169__2768, %numeric_and(__tid2165__2765, 65535<uint<64>>)

assign __tid2175__2770 = __tid2169__2768 >> 64'd2; // asn __tid2175__2770, %right_shift(__tid2169__2768, 2<uint<64>>)

assign pc__write__2777 = 1; // asn pc__write__2777, true

assign internal_pc__2783 = internal_pc; // asn internal_pc__2783, internal_pc

assign internal_pc__2784 = internal_pc__2783 + 64'd4; // asn internal_pc__2784, %addition(internal_pc__2783, 4<uint<64>>)

assign internal_pc__write__2785 = 1; // asn internal_pc__write__2785, true

assign sent_inst__2787 = 1; // asn sent_inst__2787, true

assign __tid2158__2788 = __tid2158__2760; // asn __tid2158__2788, __tid2158__2760

assign __tid2205__2792 = branch_pc_valid_pyri; // asn __tid2205__2792, branch_pc_valid_pyri

assign internal_pc__2795 = branch_pc_pyri; // asn internal_pc__2795, branch_pc_pyri

assign __tid2212__2799 = 2'd1; // asn __tid2212__2799, 1<uint<2>>

assign __tid2230__2810 = branch_pc_pyri; // asn __tid2230__2810, branch_pc_pyri

assign __tid2234__2813 = __tid2230__2810 & 64'd65535; // asn __tid2234__2813, %numeric_and(__tid2230__2810, 65535<uint<64>>)

assign __tid2240__2815 = __tid2234__2813 >> 64'd2; // asn __tid2240__2815, %right_shift(__tid2234__2813, 2<uint<64>>)

assign internal_pc__2820 = internal_pc__2795 + 64'd4; // asn internal_pc__2820, %addition(internal_pc__2795, 4<uint<64>>)

assign internal_pc__2825 = (__tid2205__2792) ? internal_pc__2820 : internal_pc;

assign internal_pc__write__2826 = 0; // asn internal_pc__write__2826, false

assign branch_pc__read__2828 = 0; // asn branch_pc__read__2828, false

assign state__2833 = (__tid2205__2792) ? __tid2212__2799 : state;

assign state__write__2834 = 0; // asn state__write__2834, false

assign pc__write__2838 = 0; // asn pc__write__2838, false

assign inst_out__2862 = 32'd0; // asn inst_out__2862, 0<uint<1>>

assign inst_out__2863 = (__tid2205__2792) ? __tid2242__2817 : inst_out__2862;

assign sent_inst__2864 = 0; // asn sent_inst__2864, false

assign internal_pc__2870 = (__tid2158__2788) ? internal_pc__2825 : internal_pc;

assign internal_pc__write__2871 = (__tid2158__2788) ? branch_pc_valid_pyri : internal_pc__write__2826;

assign branch_pc__read__2872 = (__tid2158__2788) ? branch_pc_valid_pyri : branch_pc__read__2828;

assign state__2874 = (__tid2158__2788) ? state__2833 : state;

assign state__write__2875 = (__tid2158__2788) ? branch_pc_valid_pyri : state__write__2834;

assign pc__write__2877 = (__tid2158__2788) ? branch_pc_valid_pyri : pc__write__2838;

assign inst_out__2889 = (__tid2158__2788) ? inst_out__2863 : inst_out__2862;

assign sent_inst__2890 = (__tid2158__2788) ? branch_pc_valid_pyri : sent_inst__2864;

assign inst_out__2907 = (__tid2140__2750) ? __tid2177__2772 : inst_out__2889;

assign pc__2908 = (__tid2140__2750) ? internal_pc : branch_pc_pyri;

assign pc__write__2909 = (__tid2140__2750) ? pc__write__2777 : pc__write__2877;

assign internal_pc__2912 = (__tid2140__2750) ? internal_pc__2784 : internal_pc__2870;

assign internal_pc__write__2913 = (__tid2140__2750) ? internal_pc__write__2785 : internal_pc__write__2871;

assign sent_inst__2914 = (__tid2140__2750) ? sent_inst__2787 : sent_inst__2890;

assign branch_pc__read__2917 = (__tid2140__2750) ? branch_pc__read__2828 : branch_pc__read__2872;

assign state__2919 = (__tid2140__2750) ? state : state__2874;

assign state__write__2920 = (__tid2140__2750) ? state__write__2834 : state__write__2875;

assign __tid2267__2928 = sent_inst__2914; // asn __tid2267__2928, sent_inst__2914

assign inst_out__2932 = inst_out__2907; // asn inst_out__2932, inst_out__2907

assign __tid2275__2933 = inst_out__2932[6:0]; // asn __tid2275__2933, %bit_read(inst_out__2932, 6<uint<4>>, 0<uint<1>>)

assign __tid2292__2942 = __tid2275__2933; // asn __tid2292__2942, __tid2275__2933

assign __tid2296__2944 = __tid2292__2942 == 7'd99; // asn __tid2296__2944, %is_equal(__tid2292__2942, 99<uint<7>>)

assign __tid2306__2945 = ! __tid2296__2944; // asn __tid2306__2945, %logical_not(__tid2296__2944)

assign __tid2298__2948 = __tid2275__2933; // asn __tid2298__2948, __tid2275__2933

assign __tid2302__2951 = __tid2298__2948 == 7'd103; // asn __tid2302__2951, %is_equal(__tid2298__2948, 103<uint<7>>)

assign __tid2302__2956 = 0; // asn __tid2302__2956, false

assign __tid2302__2957 = (__tid2306__2945) ? __tid2302__2951 : __tid2302__2956;

assign __tid2312__2958 = __tid2296__2944 | __tid2302__2957; // asn __tid2312__2958, %logical_or(__tid2296__2944, __tid2302__2957)

assign __tid2314__2959 = __tid2312__2958; // asn __tid2314__2959, __tid2312__2958

assign __tid2320__2962 = __tid2275__2933; // asn __tid2320__2962, __tid2275__2933

assign __tid2326__2964 = __tid2320__2962 == 7'd111; // asn __tid2326__2964, %is_equal(__tid2320__2962, 111<uint<7>>)

assign __tid2328__2965 = __tid2326__2964; // asn __tid2328__2965, __tid2326__2964

assign __tid2330__2966 = ! __tid2314__2959; // asn __tid2330__2966, %logical_not(__tid2314__2959)

assign __tid2332__2967 = __tid2330__2966 & __tid2328__2965; // asn __tid2332__2967, %logical_and(__tid2330__2966, __tid2328__2965)

assign __tid2335__2970 = 2'd2; // asn __tid2335__2970, 2<uint<2>>

assign __tid2332__2973 = __tid2332__2967; // asn __tid2332__2973, __tid2332__2967

assign fpx2667__curr_inst__2976 = inst_out__2907; // asn fpx2667__curr_inst__2976, inst_out__2907

assign __tid214__2977 = 5'd31; // asn __tid214__2977, 31<uint<5>>

assign __tid216__2978 = fpx2667__curr_inst__2976[__tid214__2977]; // asn __tid216__2978, %bit_read(fpx2667__curr_inst__2976, __tid214__2977)

assign __tid229__2982 = fpx2667__curr_inst__2976[19:12]; // asn __tid229__2982, %bit_read(fpx2667__curr_inst__2976, 19<uint<6>>, 12<uint<5>>)

assign __tid236__2983 = 5'd20; // asn __tid236__2983, 20<uint<5>>

assign __tid238__2984 = fpx2667__curr_inst__2976[__tid236__2983]; // asn __tid238__2984, %bit_read(fpx2667__curr_inst__2976, __tid236__2983)

assign __tid251__2988 = fpx2667__curr_inst__2976[30:21]; // asn __tid251__2988, %bit_read(fpx2667__curr_inst__2976, 30<uint<6>>, 21<uint<6>>)

assign __tid257__2989 = { __tid216__2978, __tid229__2982 }; // asn __tid257__2989, %concat(__tid216__2978, 1<uint<32>>, __tid229__2982, 8<uint<32>>)

assign __tid259__2990 = { __tid257__2989, __tid238__2984 }; // asn __tid259__2990, %concat(__tid257__2989, 9<uint<32>>, __tid238__2984, 1<uint<32>>)

assign __tid261__2991 = { __tid259__2990, __tid251__2988 }; // asn __tid261__2991, %concat(__tid259__2990, 10<uint<32>>, __tid251__2988, 10<uint<32>>)

assign __tid263__2992 = { __tid261__2991, 1'd0 }; // asn __tid263__2992, %concat(__tid261__2991, 20<uint<32>>, 0<uint<1>>, 1<uint<32>>)

assign fpx2667__imm__2993 = { 43'd0, __tid263__2992 }; // asn fpx2667__imm__2993, __tid263__2992

assign __tid271__2994 = 6'd20; // asn __tid271__2994, 20<uint<6>>

assign __tid273__2995 = fpx2667__imm__2993[__tid271__2994]; // asn __tid273__2995, %bit_read(fpx2667__imm__2993, __tid271__2994)

assign __tid275__2996 = __tid273__2995; // asn __tid275__2996, __tid273__2995

assign __tid281__2998 = __tid275__2996 == 1'd1; // asn __tid281__2998, %is_equal(__tid275__2996, 1<uint<1>>)

assign __tid283__2999 = __tid281__2998; // asn __tid283__2999, __tid281__2998

assign fpx2667__imm__3003 = { 43'd0, __tid263__2992 }; // asn fpx2667__imm__3003, __tid263__2992

assign fpx2667__imm__3004 = fpx2667__imm__3003 | 64'd18446744073708503040; // asn fpx2667__imm__3004, %numeric_or(fpx2667__imm__3003, 18446744073708503040<uint<64>>)

assign fpx2667__imm__3007 = (__tid283__2999) ? fpx2667__imm__3004 : { 43'd0, __tid263__2992 };

assign __tid297__3008 = fpx2667__imm__3007; // asn __tid297__3008, fpx2667__imm__3007

assign __tid2348__3010 = __tid297__3008 - 64'd4; // asn __tid2348__3010, %subtraction(__tid297__3008, 4<uint<64>>)

assign internal_pc__3011 = internal_pc__2912; // asn internal_pc__3011, internal_pc__2912

assign internal_pc__3012 = internal_pc__3011 + __tid2348__3010; // asn internal_pc__3012, %addition(internal_pc__3011, __tid2348__3010)

assign internal_pc__write__3013 = 1; // asn internal_pc__write__3013, true

assign __tid2352__3016 = 2'd1; // asn __tid2352__3016, 1<uint<2>>

assign __tid2360__3020 = 2'd1; // asn __tid2360__3020, 1<uint<2>>

assign internal_pc__3078 = (__tid2332__2973) ? internal_pc__3012 : internal_pc__2912;

assign internal_pc__write__3080 = (__tid2332__2973) ? internal_pc__write__3013 : internal_pc__write__2913;

assign state__3085 = (__tid2332__2973) ? __tid2352__3016 : __tid2360__3020;

assign state__3091 = (__tid2314__2959) ? __tid2335__2970 : state__3085;

assign state__write__3092 = 1; // asn state__write__3092, true

assign internal_pc__3121 = (__tid2314__2959) ? internal_pc__2912 : internal_pc__3078;

assign internal_pc__write__3122 = (__tid2314__2959) ? internal_pc__write__2913 : internal_pc__write__3080;

assign state__3170 = (__tid2267__2928) ? state__3091 : state__2919;

assign state__write__3171 = (__tid2267__2928) ? state__write__3092 : state__write__2920;

assign internal_pc__3200 = (__tid2267__2928) ? internal_pc__3121 : internal_pc__2912;

assign internal_pc__write__3201 = (__tid2267__2928) ? internal_pc__write__3122 : internal_pc__write__2913;

assign internal_pc__3206 = (reset_pyri) ? internal_pc__2721 : internal_pc__3200;

assign internal_pc__write__3207 = (reset_pyri) ? internal_pc__write__2722 : internal_pc__write__3201;

assign state__3210 = (reset_pyri) ? __tid2090__2724 : state__3170;

assign state__write__3211 = (reset_pyri) ? state__write__2726 : state__write__3171;

assign pc_pyro = pc__2908; // asn pc_pyro, pc__2908

assign pc__write__3251 = (reset_pyri) ? pc__write__2714 : pc__write__2909;

assign branch_pc__read__3256 = (reset_pyri) ? branch_pc__read__2715 : branch_pc__read__2917;

assign inst_pyro = inst_out__2907; // asn inst_pyro, inst_out__2907

assign inst__write__3271 = (reset_pyri) ? inst__write__2710 : sent_inst__2914;

assign inst__write__3327 = inst__write__3271; // asn inst__write__3327, inst__write__3271

assign state__write__3328 = state__write__3211; // asn state__write__3328, state__write__3211

assign internal_pc__write__3329 = internal_pc__write__3207; // asn internal_pc__write__3329, internal_pc__write__3207

assign pc__write__3331 = pc__write__3251; // asn pc__write__3331, pc__write__3251

assign branch_pc__read__3332 = branch_pc__read__3256; // asn branch_pc__read__3332, branch_pc__read__3256

assign __tid2995__3341 = ! branch_pc__read__3332; // asn __tid2995__3341, %logical_not(branch_pc__read__3332)

assign __tid2997__3342 = __tid2995__3341 | branch_pc_valid_pyri; // asn __tid2997__3342, %logical_or(__tid2995__3341, branch_pc_valid_pyri)

assign __tid2999__3343 = ! pc__write__3331; // asn __tid2999__3343, %logical_not(pc__write__3331)

assign __tid3001__3344 = ! pc_retry_pyri; // asn __tid3001__3344, %logical_not(pc_retry_pyri)

assign __tid3003__3345 = __tid2999__3343 | __tid3001__3344; // asn __tid3003__3345, %logical_or(__tid2999__3343, __tid3001__3344)

assign __tid3023__3355 = ! inst__write__3327; // asn __tid3023__3355, %logical_not(inst__write__3327)

assign __tid3025__3356 = ! inst_retry_pyri; // asn __tid3025__3356, %logical_not(inst_retry_pyri)

assign __tid3027__3357 = __tid3023__3355 | __tid3025__3356; // asn __tid3027__3357, %logical_or(__tid3023__3355, __tid3025__3356)

assign __tid3030__3358 = __tid2997__3342 & __tid3003__3345; // asn __tid3030__3358, %logical_and(__tid2997__3342, __tid3003__3345)

assign __tid3036__3361 = __tid3030__3358; // asn __tid3036__3361, __tid3030__3358

assign __tid3038__3362 = __tid3036__3361 & __tid3027__3357; // asn __tid3038__3362, %logical_and(__tid3036__3361, __tid3027__3357)

assign __block_valid__3363 = __tid3038__3362; // asn __block_valid__3363, __tid3038__3362

assign __tid3045__3370 = ! __block_valid__3363; // asn __tid3045__3370, %logical_not(__block_valid__3363)

assign pc_valid_pyro = __block_valid__3363 & pc__write__3331; // asn pc_valid_pyro, %logical_and(__block_valid__3363, pc__write__3331)

assign __tid3051__3373 = __block_valid__3363 & internal_pc__write__3329; // asn __tid3051__3373, %logical_and(__block_valid__3363, internal_pc__write__3329)

assign internal_pc_valid_pyro = 1; // asn internal_pc_valid_pyro, true

assign internal_pc_pyro = (__tid3051__3373) ? internal_pc__3206 : internal_pc;

assign __tid3056__3376 = __block_valid__3363 & state__write__3328; // asn __tid3056__3376, %logical_and(__block_valid__3363, state__write__3328)

assign state_valid_pyro = 1; // asn state_valid_pyro, true

assign state_pyro = (__tid3056__3376) ? state__3210 : state;

assign inst_valid_pyro = __block_valid__3363 & inst__write__3327; // asn inst_valid_pyro, %logical_and(__block_valid__3363, inst__write__3327)

assign __tid3063__3380 = __tid3045__3370 | __tid2995__3341; // asn __tid3063__3380, %logical_or(__tid3045__3370, __tid2995__3341)

assign branch_pc_retry_pyro = branch_pc_valid_pyri & __tid3063__3380; // asn branch_pc_retry_pyro, %logical_and(branch_pc_valid_pyri, __tid3063__3380)

fetch_block_icache fetch_block_icache_inst(.clk(clk), .__tid2177__2772(__tid2177__2772), .__tid2175__2770(__tid2175__2770), .__tid2242__2817(__tid2242__2817), .__tid2240__2815(__tid2240__2815));





  always @(posedge clk) begin
    
      state <= state_pyro; // asn state, state_pyro
    
      internal_pc <= internal_pc_pyro; // asn internal_pc, internal_pc_pyro
    
      state <= state_pyro; // asn state, state_pyro
    
      internal_pc <= internal_pc_pyro; // asn internal_pc, internal_pc_pyro
    
  end




endmodule

