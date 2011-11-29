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
* ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE. This notice    *
* including this sentence must appear on any copies of this software.*
*    Questions to Andy Salinger, agsalin@sandia.gov                  *
\********************************************************************/


#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

#include "Intrepid_FunctionSpaceTools.hpp"
#include "Intrepid_RealSpaceTools.hpp"

namespace LCM {

//**********************************************************************
template<typename EvalT, typename Traits>
ThermoPoroPlasticityResidMomentum<EvalT, Traits>::
ThermoPoroPlasticityResidMomentum(const Teuchos::ParameterList& p) :
  TotalStress      (p.get<std::string>                   ("Total Stress Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Tensor Data Layout") ),
  J           (p.get<std::string>                   ("DetDefGrad Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  Bulk          (p.get<std::string>                   ("Bulk Modulus Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  Temp         (p.get<std::string>                   ("Temperature Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  TempRef      (p.get<std::string>                   ("Reference Temperature Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  alphaSkeleton           (p.get<std::string>        ("Skeleton Thermal Expansion Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  defgrad     (p.get<std::string>                   ("DefGrad Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Tensor Data Layout") ),
  wGradBF     (p.get<std::string>                   ("Weighted Gradient BF Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("Node QP Vector Data Layout") ),
  ExResidual   (p.get<std::string>                   ("Residual Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("Node Vector Data Layout") )
{
  this->addDependentField(TotalStress);
  this->addDependentField(wGradBF);
  this->addDependentField(J);
  this->addDependentField(Bulk);
  this->addDependentField(alphaSkeleton);
  this->addDependentField(Temp);
  this->addDependentField(TempRef);
  this->addDependentField(defgrad);
  this->addEvaluatedField(ExResidual);

  if (p.isType<bool>("Disable Transient"))
    enableTransient = !p.get<bool>("Disable Transient");
  else enableTransient = true;

  if (enableTransient) {
    // Two more fields are required for transient capability
    Teuchos::RCP<PHX::DataLayout> node_qp_scalar_dl =
       p.get< Teuchos::RCP<PHX::DataLayout> >("Node QP Scalar Data Layout");
    Teuchos::RCP<PHX::DataLayout> vector_dl =
       p.get< Teuchos::RCP<PHX::DataLayout> >("QP Vector Data Layout");

    PHX::MDField<MeshScalarT,Cell,Node,QuadPoint> wBF_tmp
        (p.get<string>("Weighted BF Name"), node_qp_scalar_dl);
    wBF = wBF_tmp;
    PHX::MDField<ScalarT,Cell,QuadPoint,Dim> uDotDot_tmp
        (p.get<string>("Time Dependent Variable Name"), vector_dl);
    uDotDot = uDotDot_tmp;

   this->addDependentField(wBF);
   this->addDependentField(uDotDot);

  }


  this->setName("ThermoPoroPlasticityResidMomentum"+PHX::TypeString<EvalT>::value);

  std::vector<PHX::DataLayout::size_type> dims;
  wGradBF.fieldTag().dataLayout().dimensions(dims);
  numNodes = dims[1];
  numQPs   = dims[2];
  numDims  = dims[3];
  int worksetSize = dims[0];

  // Works space FCs
  F_inv.resize(worksetSize, numQPs, numDims, numDims);
  F_invT.resize(worksetSize, numQPs, numDims, numDims);
  JF_invT.resize(worksetSize, numQPs, numDims, numDims);
  P.resize(worksetSize, numQPs, numDims, numDims);
  thermoEPS.resize(worksetSize, numQPs, numDims, numDims);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void ThermoPoroPlasticityResidMomentum<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(TotalStress,fm);
  this->utils.setFieldData(wGradBF,fm);
  this->utils.setFieldData(J,fm);
  this->utils.setFieldData(Bulk,fm);
  this->utils.setFieldData(alphaSkeleton,fm);
  this->utils.setFieldData(Temp,fm);
  this->utils.setFieldData(TempRef,fm);
  this->utils.setFieldData(defgrad,fm);
  this->utils.setFieldData(ExResidual,fm);

  if (enableTransient) this->utils.setFieldData(uDotDot,fm);
  if (enableTransient) this->utils.setFieldData(wBF,fm);

}

//**********************************************************************
template<typename EvalT, typename Traits>
void ThermoPoroPlasticityResidMomentum<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  typedef Intrepid::FunctionSpaceTools FST;
  typedef Intrepid::RealSpaceTools<ScalarT> RST;

  RST::inverse(F_inv, defgrad);
  RST::transpose(F_invT, F_inv);
  FST::scalarMultiplyDataData<ScalarT>(JF_invT, J, F_invT);
  FST::scalarMultiplyDataData<ScalarT>(thermoEPS, alphaSkeleton , JF_invT);
  FST::tensorMultiplyDataData<ScalarT>(P, TotalStress, JF_invT);

    for (std::size_t cell=0; cell < workset.numCells; ++cell) {
      for (std::size_t node=0; node < numNodes; ++node) {
              for (std::size_t dim=0; dim<numDims; dim++)  ExResidual(cell,node,dim)=0.0;
          for (std::size_t qp=0; qp < numQPs; ++qp) {

		      if (Temp(cell,qp) == 0)
		      		  Temp(cell,qp) = TempRef(cell,qp);

            for (std::size_t i=0; i<numDims; i++) {
              for (std::size_t dim=0; dim<numDims; dim++) {
                ExResidual(cell,node,i) += (
                		                     P(cell, qp, i, dim)

                		                   - 3.0*Bulk(cell,qp)*thermoEPS(cell, qp, i, dim)
                		                        *(Temp(cell,qp) - TempRef(cell,qp))
                                             )
                		                   * wGradBF(cell, node, qp, dim);
    } } } } }


  if (workset.transientTerms && enableTransient)
    for (std::size_t cell=0; cell < workset.numCells; ++cell) {
      for (std::size_t node=0; node < numNodes; ++node) {
          for (std::size_t qp=0; qp < numQPs; ++qp) {
            for (std::size_t i=0; i<numDims; i++) {
                ExResidual(cell,node,i) += uDotDot(cell, qp, i) * wBF(cell, node, qp);
    } } } }


//  FST::integrate<ScalarT>(ExResidual, TotalStress, wGradBF, Intrepid::COMP_CPP, false); // "false" overwrites

}

//**********************************************************************
}

