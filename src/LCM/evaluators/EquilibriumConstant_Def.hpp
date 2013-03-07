//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

#include "Intrepid_FunctionSpaceTools.hpp"

namespace LCM {

//**********************************************************************
template<typename EvalT, typename Traits>
EquilibriumConstant<EvalT, Traits>::
EquilibriumConstant(const Teuchos::ParameterList& p) :
 // Rideal       (p.get<std::string>                   ("Ideal Gas Constant Name"),
//	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  temperature       (p.get<std::string>                   ("Temperature Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  Wbind       (p.get<std::string>                   ("Trap Binding Energy Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  equilibriumConstant      (p.get<std::string>                   ("Equilibrium Constant Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  Rideal(p.get<RealType>("Ideal Gas Constant"))
{
 // this->addDependentField(Rideal);
  this->addDependentField(temperature);
  this->addDependentField(Wbind);

  this->addEvaluatedField(equilibriumConstant);

  this->setName("Equilibrium Constant"+PHX::TypeString<EvalT>::value);

  Teuchos::RCP<PHX::DataLayout> scalar_dl =
    p.get< Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout");
  std::vector<PHX::DataLayout::size_type> dims;
  scalar_dl->dimensions(dims);
  numQPs  = dims[1];
}

//**********************************************************************
template<typename EvalT, typename Traits>
void EquilibriumConstant<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(equilibriumConstant,fm);
 // this->utils.setFieldData(Rideal,fm);
  this->utils.setFieldData(temperature,fm);
  this->utils.setFieldData(Wbind,fm);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void EquilibriumConstant<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  // Compute Strain tensor from displacement gradient
  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {

    	equilibriumConstant(cell,qp) = exp(Wbind(cell,qp)/
    			                                            Rideal/
    			                                            temperature(cell,qp));
    }
  }

}

//**********************************************************************
}

