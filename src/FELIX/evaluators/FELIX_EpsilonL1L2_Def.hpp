/********************************************************************\
*            Albany, Copyright (2010) Sandia Corporation             *
*                                                                    *
* Notice: This computer software was prepared by Sandia Corporation, *
* hereinafter the Contractor, under Contract DE-AC04-94AL85000 with  *
* the Department of Energy (DOE). All rights in the computer software*
* are reserved by DOE on behalf of the United States Government and  *
* the Contractor as provided in the Contract. You are authorized to  *
* use this computer software for Governmental purposes but it is not *
* to be released or distributed to the public. NEITHER THE GOVERNMENT*
* NOR THE CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR      *
* ASSUMES ANY LIABILITY L1L2R THE USE OF THIS SOFTWARE. This notice    *
* including this sentence must appear on any copies of this software.*
*    Questions to Andy Salinger, agsalin@sandia.gov                  *
\********************************************************************/


#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"
#include "Phalanx_TypeStrings.hpp"
#include "Sacado_ParameterRegistration.hpp" 

#include "Intrepid2_FunctionSpaceTools.hpp"

namespace FELIX {

//**********************************************************************
template<typename EvalT, typename Traits>
EpsilonL1L2<EvalT, Traits>::
EpsilonL1L2(const Teuchos::ParameterList& p,
            const Teuchos::RCP<Albany::Layouts>& dl) :
  Ugrad     (p.get<std::string> ("Gradient QP Variable Name"),dl->qp_vecgradient ),
  epsilonXX (p.get<std::string> ("FELIX EpsilonXX QP Variable Name"), dl->qp_scalar ), 
  epsilonYY (p.get<std::string> ("FELIX EpsilonYY QP Variable Name"), dl->qp_scalar ), 
  epsilonXY (p.get<std::string> ("FELIX EpsilonXY QP Variable Name"), dl->qp_scalar ), 
  epsilonB  (p.get<std::string> ("FELIX EpsilonB QP Variable Name"),  dl->qp_scalar ) 
{
  Teuchos::ParameterList* visc_list = 
   p.get<Teuchos::ParameterList*>("Parameter List");
  
  this->addDependentField(Ugrad);
  
  this->addEvaluatedField(epsilonXX);
  this->addEvaluatedField(epsilonYY);
  this->addEvaluatedField(epsilonXY);
  this->addEvaluatedField(epsilonB);

  std::vector<PHX::DataLayout::size_type> dims;
  dl->qp_gradient->dimensions(dims);
  numQPs  = dims[1];
  numDims = dims[2];

  Teuchos::RCP<ParamLib> paramLib = p.get< Teuchos::RCP<ParamLib> >("Parameter Library"); 
  
   this->registerSacadoParameter("Glen's Law Homotopy Parameter", paramLib);  
  this->setName("EpsilonL1L2"+PHX::typeAsString<EvalT>());
 
}

//**********************************************************************
template<typename EvalT, typename Traits>
void EpsilonL1L2<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(Ugrad,fm);
  this->utils.setFieldData(epsilonXX,fm); 
  this->utils.setFieldData(epsilonYY,fm); 
  this->utils.setFieldData(epsilonXY,fm); 
  this->utils.setFieldData(epsilonB,fm); 
}

//**********************************************************************
template<typename EvalT,typename Traits>
typename EpsilonL1L2<EvalT,Traits>::ScalarT& 
EpsilonL1L2<EvalT,Traits>::getValue(const std::string &n)
{
  return homotopyParam;
}


//**********************************************************************
template<typename EvalT, typename Traits>
void EpsilonL1L2<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {
       epsilonXX(cell,qp) = Ugrad(cell,qp,0,0); 
       epsilonYY(cell,qp) = Ugrad(cell,qp,1,1); 
       epsilonXY(cell,qp) = 0.5*(Ugrad(cell,qp,0,1) + Ugrad(cell,qp,1,0)); 
       epsilonB(cell,qp)  = epsilonXX(cell,qp)*epsilonXX(cell,qp) + epsilonYY(cell,qp)*epsilonYY(cell,qp) 
                          + epsilonXX(cell,qp)*epsilonYY(cell,qp) + epsilonXY(cell,qp)*epsilonXY(cell,qp);   
    }
  }
}
}
