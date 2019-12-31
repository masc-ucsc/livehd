//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 2/18/18.
//
#ifndef LGRAPH_ABC_CELL_HPP
#define LGRAPH_ABC_CELL_HPP

#include "pass_abc.hpp"

Pass_abc::Abc_comb LGraph_CreateNot(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateAnd(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateOr(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateXor(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateNand(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateNor(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateXnor(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateAndnot(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateOrnot(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateAoi3(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateOai3(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateAoi4(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateOai4(Abc_Ntk_t *pAig);
Pass_abc::Abc_comb LGraph_CreateMUX(Abc_Ntk_t *pAig);

void Abc_object_assign_name(Abc_Obj_t *, const std::string name);

#endif  // LGRAPH_ABC_CELL_HPP
