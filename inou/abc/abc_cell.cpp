//
// Created by birdeclipse on 2/18/18.
//

#include "abc_cell.hpp"

/**Function*************************************************************

  Synopsis    [Create an inverter gate.]

  Description [Y = ~A]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateNot(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_Not(Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0));
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an AND2X1 gate.]

  Description [ Y = A & B ]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateAnd(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateAnd((Hop_Man_t *) pAig->pManFunc, 2);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an OR2X1 gate.]

  Description [ Y = A|B ]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateOr(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateOr((Hop_Man_t *) pAig->pManFunc, 2);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an XOR gate.]

  Description [ Y = A^B ]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateXor(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateExor((Hop_Man_t *) pAig->pManFunc, 2);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an NAND2X1.]

  Description [Y = ~(A&B)]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateNand(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateAnd((Hop_Man_t *) pAig->pManFunc, 2);
	assert(pObj->pData);
	pObj->pData = Hop_Not((Hop_Obj_t *) pObj->pData);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an NOR2X1.]

  Description [Y = ~(A|B)]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateNor(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateOr((Hop_Man_t *) pAig->pManFunc, 2);
  	assert(pObj->pData);
	pObj->pData = Hop_Not((Hop_Obj_t *) pObj->pData);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an XNOR2X1 gate.]

  Description [Y = ~(A^B)]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateXnor(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateExor((Hop_Man_t *) pAig->pManFunc, 2);
    assert(pObj->pData);
	pObj->pData = Hop_Not((Hop_Obj_t *) pObj->pData);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an ANDNOT gate]

  Description [Y = A & (~B)]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateAndnot(Abc_Ntk_t *pAig) {
	//assign Y = A & (~B);
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateAnd((Hop_Man_t *) pAig->pManFunc, 2);
  	assert(pObj->pData);
	pObj->pData = Hop_And((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0),
	                      Hop_Not(Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 1)));
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an ORNOT gate.]

  Description [Y = A | (~B)]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateOrnot(Abc_Ntk_t *pAig) {
	//assign Y = A | (~B);
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateOr((Hop_Man_t *) pAig->pManFunc, 2);
  	assert(pObj->pData);
	pObj->pData = Hop_Or((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0),
	                     Hop_Not(Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 1)));
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an Aoi3 gate.]

  Description [Y = ~((A & B) | C)]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateAoi3(Abc_Ntk_t *pAig) {
	//assign Y = ~((A & B) | C);
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateOr((Hop_Man_t *) pAig->pManFunc, 3);
  	assert(pObj->pData);
	pObj->pData = Hop_Or((Hop_Man_t *) pAig->pManFunc,
	                     Hop_And((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0),
	                             Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 1)),
	                     Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 2));
	pObj->pData = Hop_Not((Hop_Obj_t *) pObj->pData);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an Oai3 gate.]

  Description [assign Y = ~((A | B) & C)]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateOai3(Abc_Ntk_t *pAig) {
	//assign Y = ~((A | B) & C);
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateAnd((Hop_Man_t *) pAig->pManFunc, 3);
  	assert(pObj->pData);
	pObj->pData = Hop_And((Hop_Man_t *) pAig->pManFunc,
	                      Hop_Or((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0),
	                             Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 1)),
	                      Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 2));
	pObj->pData = Hop_Not((Hop_Obj_t *) pObj->pData);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an Aoi4 gate.]

  Description [Y = ~((A & B) | (C & D))]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateAoi4(Abc_Ntk_t *pAig) {
	//assign Y = ~((A & B) | (C & D));
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateOr((Hop_Man_t *) pAig->pManFunc, 4);
  	assert(pObj->pData);
	pObj->pData = Hop_Or((Hop_Man_t *) pAig->pManFunc,
	                     Hop_And((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0),
	                             Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 1)),
	                     Hop_And((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 2),
	                             Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 3)));
	pObj->pData = Hop_Not((Hop_Obj_t *) pObj->pData);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an Oai4 gate.]

  Description [Y = ~((A | B) & (C | D))]

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateOai4(Abc_Ntk_t *pAig) {
	//assign Y = ~((A | B) & (C | D));
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateAnd((Hop_Man_t *) pAig->pManFunc, 4);
  	assert(pObj->pData);
	pObj->pData = Hop_And((Hop_Man_t *) pAig->pManFunc,
	                      Hop_Or((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0),
	                             Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 1)),
	                      Hop_Or((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 2),
	                             Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 3)));
	pObj->pData = Hop_Not((Hop_Obj_t *) pObj->pData);
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}

/**Function*************************************************************

  Synopsis    [Create an MUX gate.]

  Description [Y = A&(~SEL) | B & SEL]  A is the first input B is the second input SEL is the third

  SideEffects []

  SeeAlso     []

***********************************************************************/
Inou_abc::Abc_comb LGraph_CreateMUX(Abc_Ntk_t *pAig) {
	Abc_Obj_t *pObj = nullptr;
	pObj = Abc_NtkCreateNode(pAig);
	pObj->pData = Hop_CreateOr((Hop_Man_t *) pAig->pManFunc, 3);
  	assert(pObj->pData);
	pObj->pData = Hop_Or((Hop_Man_t *) pAig->pManFunc,
	                     Hop_And((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 0),
	                             Hop_Not(Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 2))),
	                     Hop_And((Hop_Man_t *) pAig->pManFunc, Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 1),
	                             Hop_IthVar((Hop_Man_t *) pAig->pManFunc, 2)));
	Abc_Obj_t *pNet = Abc_NtkCreateNet(pAig);
	Abc_ObjAddFanin(pNet, pObj);
	Inou_abc::Abc_comb TEMP = {pObj, pNet};
	return TEMP;
}








