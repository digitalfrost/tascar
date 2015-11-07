#include "acousticmodel.h"

using namespace TASCAR;
using namespace TASCAR::Acousticmodel;

mask_t::mask_t()
  : falloff(1.0),mask_inner(false)
{
}

double mask_t::gain(const pos_t& p)
{
  double d(nextpoint(p).norm());
  d = 0.5+0.5*cos(M_PI*std::min(1.0,d*falloff));
  if( mask_inner )
    return 1.0-d;
  return d;
}

pointsource_t::pointsource_t(uint32_t chunksize,double maxdist_,uint32_t sincorder_)
  : audio(chunksize), active(true), direct(true), maxdist(maxdist_), sincorder(sincorder_),rmslevel(NULL)
{
}

pointsource_t::~pointsource_t()
{
}

void pointsource_t::preprocess()
{
  if( rmslevel )
    rmslevel->append(audio);
}

void pointsource_t::add_rmslevel(TASCAR::wave_t* r)
{
  rmslevel = r;
}

doorsource_t::doorsource_t(uint32_t chunksize,double maxdist,uint32_t sincorder_)
  : pointsource_t(chunksize,maxdist,sincorder_),
    falloff(1.0),
    wnd_sqrt(false)
{
}

pos_t doorsource_t::get_effective_position(const pos_t& receiverp,double& gain)
{
  pos_t effpos(nearest(receiverp));
  pos_t receivern(effpos);
  receivern -= receiverp;
  receivern = receivern.normal();
  gain *= std::max(0.0,-dot_prod(receivern.normal(),normal));
  // gain rule: normal hanning window or sqrt
  if( wnd_sqrt )
    gain *= sqrt(0.5-0.5*cos(M_PI*std::min(1.0,::distance(effpos,receiverp)*falloff)));
  else
    gain *= 0.5-0.5*cos(M_PI*std::min(1.0,::distance(effpos,receiverp)*falloff));
  receivern *= distance;
  receivern += position;
  make_friendly_number(gain);
  return receivern;
}

pos_t pointsource_t::get_effective_position(const pos_t& receiverp,double& gain)
{
  return position;
}

diffuse_source_t::diffuse_source_t(uint32_t chunksize,wave_t& rmslevel_)
  : audio(chunksize),
    falloff(1.0),
    active(true),
    rmslevel(rmslevel_)
{
}

void diffuse_source_t::preprocess()
{
  rmslevel.append(audio.w());
}

acoustic_model_t::acoustic_model_t(double c,double fs,uint32_t chunksize,pointsource_t* src,receiver_t* receiver,const std::vector<obstacle_t*>& obstacles)
  : c_(c),
    src_(src),
    receiver_(receiver),
    receiver_data(receiver_->create_data(fs,chunksize)),
    obstacles_(obstacles),
    audio(src->audio.size()),
    chunksize(audio.size()),
    dt(1.0/std::max(1.0f,(float)chunksize)),
    distance(1.0),
    gain(1.0),
    dscale(fs/(c_*7782.0)),
    air_absorption(0.5),
    delayline((src->maxdist/c_)*fs,fs,c_,src->sincorder,64),
  airabsorption_state(0.0)
{
  pos_t prel;
  receiver_->update_refpoint(src_->get_physical_position(),src_->position,prel,distance,gain);
  gain = 1.0;
}

acoustic_model_t::~acoustic_model_t()
{
  if( receiver_data )
    delete receiver_data;
}
 
/**
   \ingroup callgraph
 */
uint32_t acoustic_model_t::process()
{
  if( receiver_->render_point && receiver_->active && src_->active && (!receiver_->gain_zero) && (src_->direct || (!receiver_->is_direct)) ){
    pos_t prel;
    double nextdistance(0.0);
    double nextgain(1.0);
    // calculate relative geometry between source and receiver:
    double srcgainmod(1.0);
    effective_srcpos = src_->get_effective_position( receiver_->position, srcgainmod );
    receiver_->update_refpoint( src_->get_physical_position(), effective_srcpos, prel, nextdistance, nextgain );
    nextgain *= srcgainmod;
    double next_air_absorption(exp(-nextdistance*dscale));
    double ddistance((std::max(0.0,nextdistance-c_*receiver_->delaycomp)-distance)*dt);
    double dgain((nextgain-gain)*dt);
    double dairabsorption((next_air_absorption-air_absorption)*dt);
    //if( first ){
    //  DEBUG(next_air_absorption);
    //  DEBUG(ddistance);
    //  DEBUG(dgain);
    //  DEBUG(dairabsorption);
    //  DEBUG(srcgainmod);
    //}
    for(uint32_t k=0;k<chunksize;k++){
      distance+=ddistance;
      gain+=dgain;
      float c1(air_absorption+=dairabsorption);
      float c2(1.0f-c1);
      // apply air absorption:
      c1 *= gain*delayline.get_dist_push(distance,src_->audio[k]);
      airabsorption_state = c2*airabsorption_state+c1;
      make_friendly_number(airabsorption_state);
      audio[k] = airabsorption_state;
    }
    if( ((gain!=0)||(dgain!=0)) ){
      //if( first ){
      //  DEBUG(audio.spldb());
      //  DEBUG(audio.maxabsdb());
      //}
      receiver_->add_pointsource(prel,audio,receiver_data);
      //if( first ){
      //  for(uint32_t k=0;k<receiver_->outchannels.size();++k){
      //    DEBUG(k);
      //    DEBUG(receiver_->outchannels[k].spldb());
      //    DEBUG(receiver_->outchannels[k].maxabsdb());
      //  }
      //}
      //first = false;
      return 1;
    }
  }else{
    delayline.add_chunk(audio);
  }
  return 0;
}

mirrorsource_t::mirrorsource_t(pointsource_t* src,reflector_t* reflector)
  : pointsource_t(src->audio.size(),src->maxdist,src->sincorder),
    src_(src),reflector_(reflector),
    //dt(1.0/std::max(1u,src->audio.size())),
    //g(0),
    //dg(0),
    lpstate(0.0)
{
}

/**
   \brief Update image source position, copy and filter audio
   \ingroup callgraph
 */
void mirrorsource_t::process()
{
  if( reflector_->active && src_->active){
    active = true;
    // calculate nominal image source position:
    p_cut = (reflector_->nearest_on_plane(src_->position));
    p_img = p_cut;
    p_img *= 2.0;
    p_img -= src_->position;
    // copy p_img to base class position:
    position = p_img;
    // calculate absorption/reflection:
    double c1(reflector_->reflectivity * (1.0-reflector_->damping));
    double c2(reflector_->damping);
    for(uint32_t k=0;k<audio.size();k++)
      audio[k] = (lpstate = lpstate*c2 + src_->audio[k]*c1);
  }else{
    active = false;
  }
}

/**
   \brief Return effective source position for a given receiver position

   Used by diffraction model.

*/
pos_t mirrorsource_t::get_effective_position(const pos_t& p_rec,double& gain)
{
  // calculate orthogonal point on plane:
  pos_t pcut_rec(reflector_->nearest_on_plane(p_rec));
  if( dot_prod( p_rec-pcut_rec, reflector_->get_normal() ) < 0 ){
    gain = 0;
    return p_img;
  }
  if( dot_prod( p_img-p_cut, reflector_->get_normal() ) > 0 ){
    gain = 0;
    return p_img;
  }
  double len_receiver(distance(pcut_rec,p_rec));
  double len_src(distance(p_cut,p_img));
  // calculate intersection:
  double ratio(len_receiver/std::max(1e-6,(len_receiver+len_src)));
  pos_t p_is(p_cut-pcut_rec);
  p_is *= ratio;
  p_is += pcut_rec;
  p_is = reflector_->nearest(p_is);
  gain = pow(std::max(0.0,dot_prod((p_rec-p_is).normal(),(p_is-p_img).normal())),2.7);
  make_friendly_number(gain);
  if( reflector_->edgereflection ){
    double len_img(distance(p_is,p_img));
    pos_t p_eff((p_is-p_rec).normal());
    p_eff *= len_img;
    p_eff += p_is;
    return p_eff;
  }
  return p_img;
}

mirror_model_t::mirror_model_t(const std::vector<pointsource_t*>& pointsources,
                               const std::vector<reflector_t*>& reflectors,
                               uint32_t order)
{
  if( order > 0 ){
    for(uint32_t ksrc=0;ksrc<pointsources.size();ksrc++)
      for(uint32_t kmir=0;kmir<reflectors.size();kmir++)
        mirrorsource.push_back(new mirrorsource_t(pointsources[ksrc],reflectors[kmir]));
  }
  uint32_t num_mirrors_start(0);
  uint32_t num_mirrors_end(mirrorsource.size());
  for(uint32_t korder=1;korder<order;korder++){
    for(uint32_t ksrc=num_mirrors_start;ksrc<num_mirrors_end;ksrc++)
      for(uint32_t kmir=0;kmir<reflectors.size();kmir++){
        if( mirrorsource[ksrc]->get_reflector() != reflectors[kmir] )
          mirrorsource.push_back(new mirrorsource_t(mirrorsource[ksrc],reflectors[kmir]));
      }
    num_mirrors_start = num_mirrors_end;
    num_mirrors_end = mirrorsource.size();
  }
}

mirror_model_t::~mirror_model_t()
{
  for(uint32_t k=0;k<mirrorsource.size();k++)
    delete mirrorsource[k];
}

reflector_t::reflector_t()
  : active(true),
    reflectivity(1.0),
    damping(0.0),
    edgereflection(true)
{
}

/**
   \brief Update all image sources
   \ingroup callgraph
 */
void mirror_model_t::process()
{
  for(uint32_t k=0;k<mirrorsource.size();k++)
    mirrorsource[k]->process();
}

std::vector<mirrorsource_t*> mirror_model_t::get_mirror_sources()
{
  std::vector<mirrorsource_t*> r;
  for(uint32_t k=0;k<mirrorsource.size();k++)
    r.push_back(mirrorsource[k]);
  return r;
}

std::vector<pointsource_t*> mirror_model_t::get_sources()
{
  std::vector<pointsource_t*> r;
  for(uint32_t k=0;k<mirrorsource.size();k++)
    r.push_back(mirrorsource[k]);
  return r;
}

world_t::world_t(double c,double fs,uint32_t chunksize,const std::vector<pointsource_t*>& sources,const std::vector<diffuse_source_t*>& diffusesources,const std::vector<reflector_t*>& reflectors,const std::vector<receiver_t*>& receivers,const std::vector<mask_t*>& masks,uint32_t mirror_order)
  : mirrormodel(sources,reflectors,mirror_order),receivers_(receivers),masks_(masks),active_pointsource(0),active_diffusesource(0)
{
  // diffuse models:
  for(uint32_t kSrc=0;kSrc<diffusesources.size();kSrc++)
    for(uint32_t kReceiver=0;kReceiver<receivers.size();kReceiver++){
      if( receivers[kReceiver]->render_diffuse )
        diffuse_acoustic_model.push_back(new diffuse_acoustic_model_t(fs,chunksize,diffusesources[kSrc],receivers[kReceiver]));
    }
  // primary sources:
  for(uint32_t kSrc=0;kSrc<sources.size();kSrc++)
    for(uint32_t kReceiver=0;kReceiver<receivers.size();kReceiver++)
      if( receivers[kReceiver]->render_point ){
        acoustic_model.push_back(new acoustic_model_t(c,fs,chunksize,sources[kSrc],receivers[kReceiver]));
      }
  // image sources:
  std::vector<mirrorsource_t*> msources(mirrormodel.get_mirror_sources());
  for(uint32_t kSrc=0;kSrc<msources.size();kSrc++)
    for(uint32_t kReceiver=0;kReceiver<receivers.size();kReceiver++)
      if( receivers[kReceiver]->render_image && receivers[kReceiver]->render_point)
        acoustic_model.push_back(new acoustic_model_t(c,fs,chunksize,msources[kSrc],receivers[kReceiver]));
  //,std::vector<obstacle_t*>(1,msources[kSrc]->get_reflector())
}

world_t::~world_t()
{
  if( acoustic_model.size() )
    for(unsigned int k=acoustic_model.size()-1;k>0;k--)
      if( acoustic_model[k] )
        delete acoustic_model[k];
  for(unsigned int k=0;k<diffuse_acoustic_model.size();k++)
    delete diffuse_acoustic_model[k];
}


/**
   \ingroup callgraph
 */
void world_t::process()
{
  uint32_t local_active_point(0);
  uint32_t local_active_diffuse(0);
  mirrormodel.process();
  // calculate mask gains:
  for(uint32_t k=0;k<receivers_.size();k++){
    double gain_inner(1.0);
    if( receivers_[k]->use_global_mask || receivers_[k]->boundingbox.active ){
      if( receivers_[k]->boundingbox.active ){
        shoebox_t maskbox;
        maskbox.size = receivers_[k]->boundingbox.size;
        receivers_[k]->boundingbox.get_6dof(maskbox.center, maskbox.orientation);
        double d(maskbox.nextpoint(receivers_[k]->position).norm());
        gain_inner *= 0.5+0.5*cos(M_PI*std::min(1.0,d/std::max(receivers_[k]->boundingbox.falloff,1e-10)));
      }
      if( receivers_[k]->use_global_mask ){
        uint32_t c_outer(0);
        double gain_outer(0.0);
        for(uint32_t km=0;km<masks_.size();km++){
          pos_t p(receivers_[k]->position);
          if( masks_[km]->mask_inner ){
            gain_inner = std::min(gain_inner,masks_[km]->gain(p));
          }else{
            c_outer++;
            gain_outer = std::max(gain_outer,masks_[km]->gain(p));
          }
        }
        if( c_outer > 0 )
          gain_inner *= gain_outer;
      }
    }
    receivers_[k]->set_next_gain(gain_inner);
  }
  // calculate acoustic model:
  for(unsigned int k=0;k<acoustic_model.size();k++)
    local_active_point += acoustic_model[k]->process();
  for(unsigned int k=0;k<diffuse_acoustic_model.size();k++)
    local_active_diffuse += diffuse_acoustic_model[k]->process();
  // apply receiver gain:
  for(uint32_t k=0;k<receivers_.size();k++)
    receivers_[k]->apply_gain();
  active_pointsource = local_active_point;
  active_diffusesource = local_active_diffuse;
}

diffuse_acoustic_model_t::diffuse_acoustic_model_t(double fs,uint32_t chunksize,diffuse_source_t* src,receiver_t* receiver)
  : src_(src),
    receiver_(receiver),
    receiver_data(receiver_->create_data(fs,chunksize)),
    audio(src->audio.size()),
    chunksize(audio.size()),
    dt(1.0/std::max(1u,chunksize)),
    gain(1.0)
{
  pos_t prel;
  double d(1.0);
  receiver_->update_refpoint(src_->center,src_->center,prel,d,gain);
}

diffuse_acoustic_model_t::~diffuse_acoustic_model_t()
{
  if( receiver_data )
    delete receiver_data;
}
 
/**
   \ingroup callgraph
 */
uint32_t diffuse_acoustic_model_t::process()
{
  pos_t prel;
  double d(0.0);
  double nextgain(1.0);
  // calculate relative geometry between source and receiver:
  receiver_->update_refpoint(src_->center,src_->center,prel,d,nextgain);
  shoebox_t box(*src_);
  //box.size = src_->size;
  box.center = pos_t();
  pos_t prel_nonrot(prel);
  prel_nonrot *= receiver_->orientation;
  d = box.nextpoint(prel_nonrot).norm();
  nextgain = 0.5+0.5*cos(M_PI*std::min(1.0,d*src_->falloff));
  if( !((gain==0) && (nextgain==0))){
    audio.rotate(src_->audio,receiver_->orientation);
    double dgain((nextgain-gain)*dt);
    for(uint32_t k=0;k<chunksize;k++){
      gain+=dgain;
      if( receiver_->active && src_->active ){
        audio.w()[k] *= gain;
        audio.x()[k] *= gain;
        audio.y()[k] *= gain;
        audio.z()[k] *= gain;
      }
    }
    if( receiver_->render_diffuse && receiver_->active && src_->active && (!receiver_->gain_zero) ){
      audio *= receiver_->diffusegain;
      receiver_->add_diffusesource(audio,receiver_data);
      return 1;
    }
  }
  return 0;
}

receiver_t::receiver_t(xmlpp::Element* xmlsrc)
  : receivermod_t(xmlsrc),
    render_point(true),
    render_diffuse(true),
    render_image(true),
    is_direct(true),
    use_global_mask(true),
    diffusegain(1.0),
    falloff(-1.0),
    delaycomp(0.0),
    active(true),
    boundingbox(find_or_add_child("boundingbox")),
    gain_zero(false),
    x_gain(1.0),
    dx_gain(0),
    dt(1),
    next_gain(1.0)
{
  GET_ATTRIBUTE(size);
  get_attribute_bool("point",render_point);
  get_attribute_bool("diffuse",render_diffuse);
  get_attribute_bool("image",render_image);
  get_attribute_bool("isdirect",is_direct);
  get_attribute_bool("globalmask",use_global_mask);
  get_attribute_db("diffusegain",diffusegain);
  GET_ATTRIBUTE(falloff);
  GET_ATTRIBUTE(delaycomp);
}

void receiver_t::write_xml()
{
  receivermod_t::write_xml();
  SET_ATTRIBUTE(size);
  set_attribute_bool("point",render_point);
  set_attribute_bool("diffuse",render_diffuse);
  set_attribute_bool("image",render_image);
  set_attribute_bool("isdirect",is_direct);
  set_attribute_bool("globalmask",use_global_mask);
  set_attribute_db("diffusegain",diffusegain);
  SET_ATTRIBUTE(falloff);
  SET_ATTRIBUTE(delaycomp);
  boundingbox.write_xml();
}

void receiver_t::prepare(double srate, uint32_t chunksize)
{
  dt = 1.0/std::max(1.0f,(float)chunksize);
  outchannels.clear();
  for(uint32_t k=0;k<get_num_channels();k++)
    outchannels.push_back(wave_t(chunksize));
}

void receiver_t::clear_output()
{
  for(uint32_t ch=0;ch<outchannels.size();ch++)
    outchannels[ch].clear();
}

/**
   \ingroup callgraph
 */
void receiver_t::add_pointsource(const pos_t& prel, const wave_t& chunk, receivermod_base_t::data_t* data)
{
  receivermod_t::add_pointsource(prel,chunk,outchannels,data);
}

/**
   \ingroup callgraph
 */
void receiver_t::add_diffusesource(const amb1wave_t& chunk, receivermod_base_t::data_t* data)
{
  receivermod_t::add_diffusesource(chunk,outchannels,data);
}

void receiver_t::update_refpoint(const pos_t& psrc_physical, const pos_t& psrc_virtual, pos_t& prel, double& distance, double& gain)
{
  
  if( (size.x!=0)&&(size.y!=0)&&(size.z!=0) ){
    prel = psrc_physical;
    prel -= position;
    prel /= orientation;
    distance = prel.norm();
    shoebox_t box;
    box.size = size;
    double sizedist = pow(size.x*size.y*size.z,0.33333);
    double d(box.nextpoint(prel).norm());
    if( falloff > 0 )
      gain = (0.5+0.5*cos(M_PI*std::min(1.0,d*falloff)))/std::max(0.1,sizedist);
    else
      gain = 1.0/std::max(1.0,d+sizedist);
  }else{
    prel = psrc_virtual;
    prel -= position;
    prel /= orientation;
    distance = prel.norm();
    gain = 1.0/std::max(0.1,distance);
  }
  make_friendly_number(gain);
}

void receiver_t::set_next_gain(double g)
{
  next_gain = g;
  gain_zero = (next_gain==0) && (x_gain==0);
}

void receiver_t::apply_gain()
{
  dx_gain = (next_gain-x_gain)*dt;
  uint32_t ch(get_num_channels());
  if( ch > 0 ){
    uint32_t psize(outchannels[0].size());
    for(uint32_t k=0;k<psize;k++){
      double g(x_gain+=dx_gain);
      for(uint32_t c=0;c<ch;c++){
        outchannels[c][k] *= g;
      }
    }
  }
}

TASCAR::Acousticmodel::boundingbox_t::boundingbox_t(xmlpp::Element* xmlsrc)
  : dynobject_t(xmlsrc),falloff(1.0),active(false)
{
  dynobject_t::get_attribute("size",size);
  dynobject_t::get_attribute("falloff",falloff);
  dynobject_t::get_attribute_bool("active",active);
}

void TASCAR::Acousticmodel::boundingbox_t::write_xml()
{
  dynobject_t::write_xml();
  dynobject_t::set_attribute("size",size);
  dynobject_t::set_attribute("falloff",falloff);
  dynobject_t::set_attribute_bool("active",active);
}

/**
   \brief Apply diffraction model 

   \param p_src Source position
   \param p_is Intersection position
   \param p_rec Receiver position
   \param audio Audio chunk
   \param c Speed of sound
   \param fs Sampling rate
   \param state Diffraction filter states

   \return Effective source position

   \ingroup callgraph
*/
pos_t diffractor_t::process(const pos_t& p_src, const pos_t& p_is, const pos_t& p_rec, wave_t& audio, double c, double fs, state_t& state)
{
  // calculate geometry:
  pos_t p_is_src(p_src-p_is);
  pos_t p_rec_is(p_is-p_rec);
  p_rec_is.normalize();
  double d_is_src(p_is_src.norm());
  if( d_is_src > 0 )
    p_is_src *= 1.0/d_is_src;
  // calculate first zero crossing frequency:
  double cos_theta(std::max(0.0,dot_prod(p_is_src,p_rec_is)));
  double sin_theta(std::max(EPS,sqrt(1.0-cos_theta*cos_theta)));
  double f0(3.8317*c/(PI2*aperture*sin_theta));
  // calculate filter coefficient increment:
  double dt(1.0/audio.n);
  double dA1((exp(-M_PI*f0/fs)-state.A1)*dt);
  // apply low pass filter to audio chunk:
  for(uint32_t k=0;k<audio.n;k++){
    state.A1 += dA1;
    double B0(1.0-state.A1);
    state.s1 = state.s1*state.A1+audio[k]*B0;
    audio[k] = state.s2 = state.s2*state.A1+state.s1*B0;
  }
  // return effective source position:
  p_rec_is *= d_is_src;
  return p_is+p_rec_is;
}

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
