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
NSNeutronEqResid<EvalT, Traits>::
NSNeutronEqResid(const Teuchos::ParameterList& p) :
  wBF         (p.get<std::string>                   ("Weighted BF Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("Node QP Scalar Data Layout") ),
  Neutron      (p.get<std::string>                   ("QP Variable Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  NeutronDiff  (p.get<std::string>                   ("Neutron Diffusion Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  wGradBF     (p.get<std::string>                   ("Weighted Gradient BF Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("Node QP Vector Data Layout") ),
  NGrad       (p.get<std::string>                   ("Gradient QP Variable Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Vector Data Layout") ),
  Absorp      (p.get<std::string>                   ("Neutron Absorption Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  Fission     (p.get<std::string>                  ("Neutron Fission Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  nu          (p.get<std::string>                  ("Neutrons per Fission Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  NResidual   (p.get<std::string>                   ("Residual Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("Node Scalar Data Layout") ),
  haveNeutSource  (p.get<bool>("Have Neutron Source"))
{

  this->addDependentField(wBF);
  this->addDependentField(wGradBF);
  this->addDependentField(Neutron);
  this->addDependentField(NGrad);
  this->addDependentField(NeutronDiff);
  this->addDependentField(Absorp);
  this->addDependentField(Fission);
  this->addDependentField(nu);
  
  if (haveNeutSource) {
    Source = PHX::MDField<ScalarT,Cell,QuadPoint>(
      p.get<std::string>("Source Name"),
      p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") );
    this->addDependentField(Source);
  }

  this->addEvaluatedField(NResidual);

  Teuchos::RCP<PHX::DataLayout> vector_dl =
    p.get< Teuchos::RCP<PHX::DataLayout> >("QP Vector Data Layout");
  std::vector<PHX::DataLayout::size_type> dims;
  vector_dl->dimensions(dims);
  numQPs  = dims[1];
  numDims = dims[2];

  // Allocate workspace
  flux.resize(dims[0], numQPs, numDims);
  abscoeff.resize(dims[0], numQPs);
 
  this->setName("NSNeutronEqResid" );
}

//**********************************************************************
template<typename EvalT, typename Traits>
void NSNeutronEqResid<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(wBF,fm);
  this->utils.setFieldData(wGradBF,fm);
  this->utils.setFieldData(Neutron,fm);
  this->utils.setFieldData(NGrad,fm);
  this->utils.setFieldData(NeutronDiff,fm);
  this->utils.setFieldData(Absorp,fm);
  this->utils.setFieldData(Fission,fm);
  this->utils.setFieldData(nu,fm);
  if (haveNeutSource)  this->utils.setFieldData(Source,fm);

  this->utils.setFieldData(NResidual,fm);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void NSNeutronEqResid<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  typedef Intrepid2::FunctionSpaceTools FST;

  FST::scalarMultiplyDataData<ScalarT> (flux, NeutronDiff, NGrad);

  FST::integrate<ScalarT>(NResidual, flux, wGradBF, Intrepid2::COMP_CPP, false); // "false" overwrites
  
  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {
      abscoeff(cell,qp) = 
	(Absorp(cell,qp) - nu(cell,qp)*Fission(cell,qp)) * Neutron(cell,qp);
      if (haveNeutSource) abscoeff(cell,qp) -= Source(cell,qp);
    }
  }

  FST::integrate<ScalarT>(NResidual, abscoeff, wBF, Intrepid2::COMP_CPP, true); // "true" sums into

}

//**********************************************************************
}

