//*****************************************************************//
//    Albany 3.0:  Copyright 2016 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//


#include "AAdapt_AbstractAdapter.hpp"

// Generic implementations that can be used by derived adapters

namespace AAdapt {

//----------------------------------------------------------------------------
AAdapt::AbstractAdapter::
AbstractAdapter(const Teuchos::RCP<Teuchos::ParameterList>& params,
                const Teuchos::RCP<ParamLib>& param_lib,
                Albany::StateManager& state_mgr,
                const Teuchos::RCP<const Teuchos_Comm>& commT) :
  output_stream_(Teuchos::VerboseObjectBase::getDefaultOStream()),
  adapt_params_(params),
  param_lib_(param_lib),
  state_mgr_(state_mgr),
  commT_(commT)
{}

//----------------------------------------------------------------------------
Teuchos::RCP<Teuchos::ParameterList>
AAdapt::AbstractAdapter::
getGenericAdapterParams(std::string listname) const {
  Teuchos::RCP<Teuchos::ParameterList> valid_pl =
    Teuchos::rcp(new Teuchos::ParameterList(listname));

  valid_pl->set<std::string>("Method",
                             "",
                             "String to designate adapter class");

  return valid_pl;
}
//----------------------------------------------------------------------------
}
