////////////////////////////////////////////////////////////////////////
// Class:       RFFHitFinder
// Module Type: producer
// File:        RFFHitFinder_module.cc
//
// Generated at Fri Jan 30 17:27:31 2015 by Wesley Ketchum using artmod
// from cetpkgsupport v1_08_02.
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include <memory>

#include "RFFHitFinderAlg.h"

namespace hit{
  class RFFHitFinder;
}

namespace hit{

  class RFFHitFinder : public art::EDProducer {
  public:
    explicit RFFHitFinder(fhicl::ParameterSet const & p);
    // The destructor generated by the compiler is fine for classes
    // without bare pointers or other resource use.
    
    // Plugins should not be copied or assigned.
    RFFHitFinder(RFFHitFinder const &) = delete;
    RFFHitFinder(RFFHitFinder &&) = delete;
    RFFHitFinder & operator = (RFFHitFinder const &) = delete;
    RFFHitFinder & operator = (RFFHitFinder &&) = delete;
    
    // Required functions.
    void produce(art::Event & e) override;
    
    // Selected optional functions.
    void beginJob() override;
    
  private:
    
    std::string fWireModuleLabel;
    bool        fMakeWireHitAssocs;
    RFFHitFinderAlg fAlg;
  };
  

  RFFHitFinder::RFFHitFinder(fhicl::ParameterSet const & p)
    :
    fWireModuleLabel(p.get<std::string>("WireModuleLabel")),
    fAlg(p.get<fhicl::ParameterSet>("RFFHitFinderAlgParams"))
  {
    produces< std::vector<recob::Hit> >();
  }
  
  void RFFHitFinder::produce(art::Event & e)
  {
    art::ServiceHandle<geo::Geometry> geoHandle;

    art::Handle< std::vector<recob::Wire> > wireHandle;
    e.getByLabel(fWireModuleLabel,wireHandle);

    std::unique_ptr< std::vector<recob::Hit> > hitCollection(new std::vector<recob::Hit>);

    fAlg.Run(*wireHandle,*hitCollection,*geoHandle);

    e.put(std::move(hitCollection));
  }
  
  void RFFHitFinder::beginJob()
  {
    art::ServiceHandle<geo::Geometry> geoHandle;
    geo::Geometry const& geo(*geoHandle);
    fAlg.SetFitterParamsVectors(geo);
  }

}

DEFINE_ART_MODULE(hit::RFFHitFinder)