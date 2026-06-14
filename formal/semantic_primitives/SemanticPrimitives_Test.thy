(*  pass.isabelle hard CI gate
    Every helper definition in SemanticPrimitives.thy must be eval-clean on
    concrete regression lemmas before pass.isabelle is allowed to emit calls to it.
*)

theory SemanticPrimitives_Test
  imports SemanticPrimitives
begin

lemma bit_mask_at_concrete:
  "bit_mask_at 3 = (8 :: 8 word)"
  by eval

lemma bit_mask_at_out_of_range_zero:
  "bit_mask_at 8 = (0 :: 8 word)"
  by eval

lemma sem_set_mask_concrete:
  "sem_set_mask (0xF0 :: 8 word) (0x0F :: 8 word) (0xA :: 8 word) = 0xFA"
  by eval

lemma sem_set_mask_oob_mask_bit_ignored_when_value_one:
  "sem_set_mask (0x3 :: 2 word) (0x105 :: 9 word) (0x7 :: 3 word) = (0x3 :: 2 word)"
  by eval

lemma sem_set_mask_oob_mask_bit_ignored_when_value_zero:
  "sem_set_mask (0x3 :: 2 word) (0x105 :: 9 word) (0x0 :: 3 word) = (0x2 :: 2 word)"
  by eval

lemma sem_get_mask_concrete:
  "sem_get_mask (0xAB :: 8 word) (0x0F :: 8 word) = (0xB :: 4 word)"
  by eval

lemma sem_get_mask_noncontiguous:
  "sem_get_mask (0xA5 :: 8 word) (0x05 :: 8 word) = (0x3 :: 2 word)"
  by eval

lemma sem_get_mask_high_bit_to_one:
  "sem_get_mask (0x80000000 :: 32 word) (0x80000000 :: 32 word) = (1 :: 1 word)"
  by eval

lemma sem_get_mask_mask_wider_than_source:
  "sem_get_mask (0x3 :: 2 word) (0x105 :: 9 word) = (0x1 :: 3 word)"
  by eval

lemma sem_get_mask_zero_pads_when_output_wider:
  "sem_get_mask (0xA5 :: 8 word) (0x05 :: 8 word) = (0x3 :: 4 word)"
  by eval

lemma trunc_div_int_pos_pos:
  "trunc_div_int 7 2 = 3"
  by eval

lemma trunc_div_int_neg_pos:
  "trunc_div_int (-7) 2 = -3"
  by eval

lemma trunc_div_int_pos_neg:
  "trunc_div_int 7 (-2) = -3"
  by eval

lemma trunc_div_int_neg_neg:
  "trunc_div_int (-7) (-2) = 3"
  by eval

lemma sem_sdiv_neg_pos_8w:
  "sem_sdiv ((-7) :: 8 word) (2 :: 8 word) = word_of_int (-3)"
  by eval

lemma sem_sdiv_dbz_8w:
  "sem_sdiv ((-7) :: 8 word) (0 :: 8 word) = 0"
  by eval

lemma sem_udiv_dbz_8w:
  "sem_udiv (7 :: 8 word) (0 :: 8 word) = 0"
  by eval

lemma sem_udiv_pos_8w:
  "sem_udiv (10 :: 8 word) (3 :: 8 word) = 3"
  by eval

lemma const_pos_concrete:
  "(5 :: 8 word) = 5"
  by eval

lemma const_neg_concrete:
  "((word_of_int (-5)) :: 8 word) = 251"
  by eval

lemma const_neg_one_64:
  "((word_of_int (-1)) :: 64 word) = (18446744073709551615 :: 64 word)"
  by eval

lemma sem_sra_neg_8w:
  "sem_sra ((0x80) :: 8 word) (1 :: 3 word) = (0xC0 :: 8 word)"
  by eval

lemma sem_sra_pos_8w:
  "sem_sra ((0x40) :: 8 word) (1 :: 3 word) = (0x20 :: 8 word)"
  by eval

lemma flop_next_reset:
  "flop_next True (0 :: 8 word) True (0xAA :: 8 word) (0x55 :: 8 word) = 0"
  by eval

lemma flop_next_enable_load:
  "flop_next False (0 :: 8 word) True (0xAA :: 8 word) (0x55 :: 8 word) = 0xAA"
  by eval

lemma flop_next_enable_off:
  "flop_next False (0 :: 8 word) False (0xAA :: 8 word) (0x55 :: 8 word) = 0x55"
  by eval

lemma sem_ror_bool_empty:
  "sem_ror_bool [] = (0 :: 1 word)"
  by eval

lemma sem_ror_bool_one_true:
  "sem_ror_bool [True] = (1 :: 1 word)"
  by eval

lemma sem_ror_bool_one_false:
  "sem_ror_bool [False] = (0 :: 1 word)"
  by eval

lemma sem_ror_bool_mixed:
  "sem_ror_bool [False, False, True, False] = (1 :: 1 word)"
  by eval

definition test_mem :: "(4, 8) mem" where
  "test_mem = (%_. 0)"

lemma mem_read_default:
  "mem_read test_mem (3 :: 4 word) = (0 :: 8 word)"
  by (eval)

lemma mem_write_read_same:
  "mem_read (mem_write test_mem (3 :: 4 word) (0xAA :: 8 word)) 3 = 0xAA"
  by eval

lemma mem_write_read_other:
  "mem_read (mem_write test_mem (3 :: 4 word) (0xAA :: 8 word)) (4 :: 4 word) = 0"
  by eval

lemma byte_enable_mask_8bit_lanes:
  "byte_enable_mask (0b0101 :: 4 word) 8 = (0x00FF00FF :: 32 word)"
  by eval

lemma masked_word_update_bytes:
  "masked_word_update (0x11223344 :: 32 word) (0xAABBCCDD :: 32 word) (0b0101 :: 4 word) 8 =
   (0x11BB33DD :: 32 word)"
  by eval

lemma mem_write_be_updates_selected_bytes:
  "mem_read (mem_write_be test_mem (2 :: 4 word) (0xAB :: 8 word) (1 :: 1 word) 8) 2 = 0xAB"
  by eval

lemma sram_1rw_read_first_collision:
  "sram_1rw_read_first True (3 :: 4 word) (0xAA :: 8 word)
     (mem_write test_mem 3 (0x55 :: 8 word)) =
   (mem_write (mem_write test_mem 3 (0x55 :: 8 word)) 3 0xAA, 0x55)"
  by eval

lemma sram_1rw_write_first_collision:
  "sram_1rw_write_first True (3 :: 4 word) (0xAA :: 8 word)
     (mem_write test_mem 3 (0x55 :: 8 word)) =
   (mem_write (mem_write test_mem 3 (0x55 :: 8 word)) 3 0xAA, 0xAA)"
  by eval

lemma sram_1r1w_read_first_same_addr:
  "sram_1r1w_read_first True (3 :: 4 word) (0xAA :: 8 word) 3
     (mem_write test_mem 3 (0x55 :: 8 word)) =
   (mem_write (mem_write test_mem 3 (0x55 :: 8 word)) 3 0xAA, 0x55)"
  by eval

lemma sram_1r1w_write_first_same_addr:
  "sram_1r1w_write_first True (3 :: 4 word) (0xAA :: 8 word) 3
     (mem_write test_mem 3 (0x55 :: 8 word)) =
   (mem_write (mem_write test_mem 3 (0x55 :: 8 word)) 3 0xAA, 0xAA)"
  by eval

lemma sram_sync_read_reg_next_enable:
  "sram_sync_read_reg_next True (0xAA :: 8 word) (0x55 :: 8 word) = 0xAA"
  by eval

lemma sram_sync_read_reg_next_hold:
  "sram_sync_read_reg_next False (0xAA :: 8 word) (0x55 :: 8 word) = 0x55"
  by eval

end
