//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 2/18/18.
//
#include "inou_abc.hpp"

#ifndef LGRAPH_ABC_CELL_HPP
#define LGRAPH_ABC_CELL_HPP

Inou_abc::Abc_comb LGraph_CreateNot(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateAnd(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateOr(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateXor(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateNand(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateNor(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateXnor(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateAndnot(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateOrnot(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateAoi3(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateOai3(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateAoi4(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateOai4(Abc_Ntk_t *pAig);

Inou_abc::Abc_comb LGraph_CreateMUX(Abc_Ntk_t *pAig);

#endif //LGRAPH_ABC_CELL_HPP
