//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Teuchos_TestForException.hpp"
#include "Teuchos_VerboseObject.hpp"
#include "Teuchos_RCP.hpp"
#include "Phalanx_DataLayout.hpp"
#include "Sacado_ParameterRegistration.hpp"

#include "Intrepid2_FunctionSpaceTools.hpp"

namespace Aeras {

//**********************************************************************
template<typename EvalT, typename Traits>
XZScalarAdvectionResid<EvalT, Traits>::
XZScalarAdvectionResid(const Teuchos::ParameterList& p,
              const Teuchos::RCP<Albany::Layouts>& dl) :
  wBF      (p.get<std::string> ("Weighted BF Name"), dl->node_qp_scalar),
  wGradBF  (p.get<std::string> ("Weighted Gradient BF Name"),dl->node_qp_gradient),
  rho      (p.get<std::string> ("QP Variable Name"), dl->qp_scalar),
  rhoGrad  (p.get<std::string> ("Gradient QP Variable Name"), dl->qp_gradient),
  rhoDot   (p.get<std::string> ("QP Time Derivative Variable Name"), dl->qp_scalar),
  coordVec (p.get<std::string> ("QP Coordinate Vector Name"), dl->qp_gradient),
  Residual (p.get<std::string> ("Residual Name"), dl->node_scalar)
{

  Teuchos::ParameterList* eulerList = p.get<Teuchos::ParameterList*>("XZScalarAdvection Problem");
  Re = eulerList->get<double>("Reynolds Number", 1.0); //Default: Re=1

  this->addDependentField(rho);
  this->addDependentField(rhoGrad);
  this->addDependentField(rhoDot);
  this->addDependentField(wBF);
  this->addDependentField(wGradBF);
  this->addDependentField(coordVec);

  this->addEvaluatedField(Residual);


  this->setName("Aeras::XZScalarAdvectionResid"+PHX::typeAsString<EvalT>());

  std::vector<int> dims;
  wGradBF.fieldTag().dataLayout().dimensions(dims);
  numNodes = dims[1];
  numQPs   = dims[2];
  numDims  = dims[3];

  // Register Reynolds number as Sacado-ized Parameter
  Teuchos::RCP<ParamLib> paramLib = p.get<Teuchos::RCP<ParamLib> >("Parameter Library");
  this->registerSacadoParameter("Reynolds Number", paramLib);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void XZScalarAdvectionResid<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(rho,fm);
  this->utils.setFieldData(rhoGrad,fm);
  this->utils.setFieldData(rhoDot,fm);
  this->utils.setFieldData(wBF,fm);
  this->utils.setFieldData(wGradBF,fm);
  this->utils.setFieldData(coordVec,fm);

  this->utils.setFieldData(Residual,fm);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void XZScalarAdvectionResid<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  // Constants 
  L = 2.5e6 // Latent Heat J/kg
  cp = 1004.64 // Specfic Heat J/kg/K
  
  std::vector<ScalarT> vel(2);
  for (std::size_t i=0; i < Residual.size(); ++i) Residual(i)=0.0;

  for (std::size_t cell=0; cell < workset.numCells; ++cell) {
    for (std::size_t qp=0; qp < numQPs; ++qp) {
  
      if (coordVec(cell,qp,1) > 5.0) vel[0] = Re;
      else                           vel[0] = 0.0;
      vel[1] = 0.0;

      for (std::size_t node=0; node < numNodes; ++node) {
          // Transient Term
          // Residual(cell,node) += rhoDot(cell,qp)*wBF(cell,node,qp);
          Residual(cell,node,0) += rhoDot(cell,qp)*wBF(cell,node,qp);
          Residual(cell,node,1) += tempDot(cell,qp)*wBF(cell,node,qp);
          Residual(cell,node,2) += qvDot(cell,qp)*wBF(cell,node,qp);
          Residual(cell,node,3) += qcDot(cell,qp)*wBF(cell,node,qp);

          // Compute saturation mixing ratio for condensation rate with Teton's formula
          // Saturation vapor pressure, temp in Celcius, equation valid over [-35,35] with a 3% error
          qvs = 3.8 / rhoDot(cell,qp) * exp(17.27 * (tempGrad(cell,qp) - 273.)/(tempGrad(cell,qp) - 36.));

          C = max( (qvDot(cell,qp) - qvs)/( 1. + qvs*((4093.*L)/(cp*tempDot(cell,qp)-36.)^2.) ) , -qcDot(cell,qp) );

 
          Tv = T * (1 + 0.6*qv);

          // Advection Term
          for (std::size_t j=0; j < numDims; ++j) {
            Residual(cell,node,0) += vel[j]*rhoGrad(cell,qp,j)*wBF(cell,node,qp);
            Residual(cell,node,1) += vel[j]*tempGrad(cell,qp,j)*wBF(cell,node,qp)+L/cp*C;
            Residual(cell,node,2) += vel[j]*qvGrad(cell,qp,j)*wBF(cell,node,qp)-C;
            Residual(cell,node,3) += vel[j]*qcGrad(cell,qp,j)*wBF(cell,node,qp)+C;
          }
      }
    }
  }
}

//**********************************************************************
// Provide Access to Parameter for sensitivity/optimization/UQ
template<typename EvalT,typename Traits>
typename XZScalarAdvectionResid<EvalT,Traits>::ScalarT&
XZScalarAdvectionResid<EvalT,Traits>::getValue(const std::string &n)
{
  return Re;
}
//**********************************************************************
}
