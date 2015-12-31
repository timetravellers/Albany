//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

#include "Intrepid2_FunctionSpaceTools.hpp"

namespace PHAL {

//**********************************************************************
template<typename EvalT, typename Traits>
DOFTensorInterpolation<EvalT, Traits>::
DOFTensorInterpolation(const Teuchos::ParameterList& p,
                    const Teuchos::RCP<Albany::Layouts>& dl) :
  val_node    (p.get<std::string>  ("Variable Name"), dl->node_tensor),
  BF          (p.get<std::string>  ("BF Name"),  dl->node_qp_scalar),
  val_qp      (p.get<std::string>  ("Variable Name"), dl->qp_tensor)
{
  this->addDependentField(val_node);
  this->addDependentField(BF);
  this->addEvaluatedField(val_qp);

  this->setName("DOFTensorInterpolation"+PHX::typeAsString<EvalT>());
  std::vector<PHX::DataLayout::size_type> dims;
  BF.fieldTag().dataLayout().dimensions(dims);
  numNodes = dims[1];
  numQPs   = dims[2];

  val_node.fieldTag().dataLayout().dimensions(dims);
  vecDim   = dims[2];
}

//**********************************************************************
template<typename EvalT, typename Traits>
void DOFTensorInterpolation<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(val_node,fm);
  this->utils.setFieldData(BF,fm);
  this->utils.setFieldData(val_qp,fm);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void DOFTensorInterpolation<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {
      for (std::size_t i=0; i<vecDim; i++) {
        for (std::size_t j=0; j<vecDim; j++) {
          // Zero out for node==0; then += for node = 1 to numNodes
          typename PHAL::Ref<ScalarT>::type vqp = val_qp(cell,qp,i,j);
          vqp = val_node(cell, 0, i, j) * BF(cell, 0, qp);
          for (std::size_t node=1; node < numNodes; ++node) {
            vqp += val_node(cell, node, i, j) * BF(cell, node, qp);
          } 
        }
      } 
    } 
  }
}

//**********************************************************************
template<typename Traits>
DOFTensorInterpolation<PHAL::AlbanyTraits::Jacobian, Traits>::
DOFTensorInterpolation(const Teuchos::ParameterList& p,
                    const Teuchos::RCP<Albany::Layouts>& dl) :
  val_node    (p.get<std::string>  ("Variable Name"), dl->node_tensor),
  BF          (p.get<std::string>  ("BF Name"),  dl->node_qp_scalar),
  val_qp      (p.get<std::string>  ("Variable Name"), dl->qp_tensor)
{
  this->addDependentField(val_node);
  this->addDependentField(BF);
  this->addEvaluatedField(val_qp);

  this->setName("DOFTensorInterpolation"+PHX::typeAsString<PHAL::AlbanyTraits::Jacobian>());
  std::vector<PHX::DataLayout::size_type> dims;
  BF.fieldTag().dataLayout().dimensions(dims);
  numNodes = dims[1];
  numQPs   = dims[2];

  val_node.fieldTag().dataLayout().dimensions(dims);
  vecDim   = dims[2];

  offset = p.get<int>("Offset of First DOF");
}

//**********************************************************************
template<typename Traits>
void DOFTensorInterpolation<PHAL::AlbanyTraits::Jacobian, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(val_node,fm);
  this->utils.setFieldData(BF,fm);
  this->utils.setFieldData(val_qp,fm);
}

//**********************************************************************
template<typename Traits>
void DOFTensorInterpolation<PHAL::AlbanyTraits::Jacobian, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  const int num_dof = val_node(0,0,0,0).size();
  const int neq = workset.wsElNodeEqID[0][0].size();
  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {
      for (std::size_t i=0; i<vecDim; i++) {
        for (std::size_t j=0; j<vecDim; j++) {
          // Zero out for node==0; then += for node = 1 to numNodes
          typename PHAL::Ref<ScalarT>::type vqp = val_qp(cell,qp,i,j);
#ifdef ALBANY_MESH_DEPENDS_ON_SOLUTION
          vqp = val_node(cell, 0, i, j) * BF(cell, 0, qp);
#else
	        vqp = ScalarT(num_dof, val_node(cell, 0, i, j).val() * BF(cell, 0, qp));
          vqp.fastAccessDx(offset+i*vecDim+j) = val_node(cell, 0, i, j).fastAccessDx(offset+i*vecDim+j) * BF(cell, 0, qp);
#endif
          for (std::size_t node=1; node < numNodes; ++node) {
#ifdef ALBANY_MESH_DEPENDS_ON_SOLUTION
            vqp += val_node(cell, node, i, j) * BF(cell, node, qp);
#else
            vqp.val() += val_node(cell, node, i, j).val() * BF(cell, node, qp);
            vqp.fastAccessDx(neq*node+offset+i*vecDim+j) 
              += val_node(cell, node, i, j).fastAccessDx(neq*node+offset+i*vecDim+j) * BF(cell, node, qp);
#endif
          } 
        }
      } 
    } 
  }
}
//**********************************************************************

#ifdef ALBANY_SG
template<typename Traits>
DOFTensorInterpolation<PHAL::AlbanyTraits::SGJacobian, Traits>::
DOFTensorInterpolation(const Teuchos::ParameterList& p,
                    const Teuchos::RCP<Albany::Layouts>& dl) :
  val_node    (p.get<std::string>  ("Variable Name"), dl->node_tensor),
  BF          (p.get<std::string>  ("BF Name"),  dl->node_qp_scalar),
  val_qp      (p.get<std::string>  ("Variable Name"), dl->qp_tensor)
{
  this->addDependentField(val_node);
  this->addDependentField(BF);
  this->addEvaluatedField(val_qp);

  this->setName("DOFTensorInterpolation"+PHX::typeAsString<PHAL::AlbanyTraits::SGJacobian>());
  std::vector<PHX::DataLayout::size_type> dims;
  BF.fieldTag().dataLayout().dimensions(dims);
  numNodes = dims[1];
  numQPs   = dims[2];

  val_node.fieldTag().dataLayout().dimensions(dims);
  vecDim   = dims[2];

  offset = p.get<int>("Offset of First DOF");
}

//**********************************************************************
template<typename Traits>
void DOFTensorInterpolation<PHAL::AlbanyTraits::SGJacobian, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(val_node,fm);
  this->utils.setFieldData(BF,fm);
  this->utils.setFieldData(val_qp,fm);
}

//**********************************************************************
template<typename Traits>
void DOFTensorInterpolation<PHAL::AlbanyTraits::SGJacobian, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  const int num_dof = val_node(0,0,0,0).size();
  const int neq = workset.wsElNodeEqID[0][0].size();
  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {
      for (std::size_t i=0; i<vecDim; i++) {
        for (std::size_t j=0; j<vecDim; j++) {
          // Zero out for node==0; then += for node = 1 to numNodes
          typename PHAL::Ref<ScalarT>::type vqp = val_qp(cell,qp,i,j);
	  vqp = ScalarT(num_dof, val_node(cell, 0, i, j).val() * BF(cell, 0, qp));
          vqp.fastAccessDx(offset+i*vecDim+j) = val_node(cell, 0, i, j).fastAccessDx(offset+i*vecDim+j) * BF(cell, 0, qp);
          for (std::size_t node=1; node < numNodes; ++node) {
            vqp.val() += val_node(cell, node, i, j).val() * BF(cell, node, qp);
            vqp.fastAccessDx(neq*node+offset+i*vecDim+j) 
              += val_node(cell, node, i, j).fastAccessDx(neq*node+offset+i*vecDim+j) * BF(cell, node, qp);
          } 
        }
      } 
    } 
  }
}
//**********************************************************************
#endif

#ifdef ALBANY_ENSEMBLE
template<typename Traits>
DOFTensorInterpolation<PHAL::AlbanyTraits::MPJacobian, Traits>::
DOFTensorInterpolation(const Teuchos::ParameterList& p,
                    const Teuchos::RCP<Albany::Layouts>& dl) :
  val_node    (p.get<std::string>  ("Variable Name"), dl->node_tensor),
  BF          (p.get<std::string>  ("BF Name"),  dl->node_qp_scalar),
  val_qp      (p.get<std::string>  ("Variable Name"), dl->qp_tensor)
{
  this->addDependentField(val_node);
  this->addDependentField(BF);
  this->addEvaluatedField(val_qp);

  this->setName("DOFTensorInterpolation"+PHX::typeAsString<PHAL::AlbanyTraits::MPJacobian>());
  std::vector<PHX::DataLayout::size_type> dims;
  BF.fieldTag().dataLayout().dimensions(dims);
  numNodes = dims[1];
  numQPs   = dims[2];

  val_node.fieldTag().dataLayout().dimensions(dims);
  vecDim   = dims[2];

  offset = p.get<int>("Offset of First DOF");
}

//**********************************************************************
template<typename Traits>
void DOFTensorInterpolation<PHAL::AlbanyTraits::MPJacobian, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(val_node,fm);
  this->utils.setFieldData(BF,fm);
  this->utils.setFieldData(val_qp,fm);
}

//**********************************************************************
template<typename Traits>
void DOFTensorInterpolation<PHAL::AlbanyTraits::MPJacobian, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  const int num_dof = val_node(0,0,0,0).size();
  const int neq = workset.wsElNodeEqID[0][0].size();
  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {
      for (std::size_t i=0; i<vecDim; i++) {
        for (std::size_t j=0; j<vecDim; j++) {
          // Zero out for node==0; then += for node = 1 to numNodes
          typename PHAL::Ref<ScalarT>::type vqp = val_qp(cell,qp,i,j);
	  vqp = ScalarT(num_dof, val_node(cell, 0, i, j).val() * BF(cell, 0, qp));
          vqp.fastAccessDx(offset+i*vecDim+j) = val_node(cell, 0, i, j).fastAccessDx(offset+i*vecDim+j) * BF(cell, 0, qp);
          for (std::size_t node=1; node < numNodes; ++node) {
            vqp.val() += val_node(cell, node, i, j).val() * BF(cell, node, qp);
            vqp.fastAccessDx(neq*node+offset+i*vecDim+j) 
              += val_node(cell, node, i, j).fastAccessDx(neq*node+offset+i*vecDim+j) * BF(cell, node, qp);
          } 
        }
      } 
    } 
  }
}
#endif

//**********************************************************************

}
