template <class Type>
Type trans(Type x){
  return Type(2)/(Type(1) + exp(-Type(2) * x)) - Type(1);
}

template <class Type>
Type jacobiUVtrans( array<Type> logF){
  int nr=logF.cols(); 
  int nc=logF.rows();
  matrix<Type> A(nc,nc);
  for(int i=0; i<nc; ++i){
    for(int j=0; j<nc; ++j){
      A(i,j) = -1;
    }
  }
  for(int i=0; i<nc; ++i){
    A(0,i)=1;
  }
  for(int i=1; i<nc; ++i){
    A(i,i-1)=nc-1;
  }
  A/=nc;
  
  return nr*log(CppAD::abs(A.determinant()));
}

template <class Type>
Type nllF(confSet &conf, paraSet<Type> &par, array<Type> &logF, array<Type> &logW, data_indicator<vector<Type>,Type> &keep, objective_function<Type> *of){
  using CppAD::abs;
  Type nll=0; 
  int stateDimF=logF.dim[0];
  int timeSteps=logF.dim[1];
  int stateDimN=conf.keyLogFsta.dim[1];
  vector<Type> sdLogFsta=exp(par.logSdLogFsta);
  array<Type> resF(stateDimF,timeSteps-1);
  matrix<Type> fvar(stateDimF,stateDimF);
  matrix<Type> fcor(stateDimF,stateDimF);
  vector<Type> fsd(stateDimF);  

  
  if(conf.corFlag==3 || conf.corFlag==4){
    return(nllFseparable(conf, par, logF,logW, keep ,of));
  }
  
  if(conf.corFlag==0){
    fcor.setZero();
  }

  for(int i=0; i<stateDimF; ++i){
    fcor(i,i)=1.0;
  }

  if(conf.corFlag==1){
    for(int i=0; i<stateDimF; ++i){
      for(int j=0; j<i; ++j){
        fcor(i,j)=trans(par.itrans_rho(0));
        fcor(j,i)=fcor(i,j);
      }
    } 
  }

  if(conf.corFlag==2){
    for(int i=0; i<stateDimF; ++i){
      for(int j=0; j<i; ++j){
        fcor(i,j)=pow(trans(par.itrans_rho(0)),abs(Type(i-j)));
        fcor(j,i)=fcor(i,j);
      }
    } 
  }

  int i,j;
  for(i=0; i<stateDimF; ++i){
    for(j=0; j<stateDimN; ++j){
      if(conf.keyLogFsta(0,j)==i)break;
    }
    fsd(i)=sdLogFsta(conf.keyVarF(0,j));
  }
 
  for(i=0; i<stateDimF; ++i){
    for(j=0; j<stateDimF; ++j){
      fvar(i,j)=fsd(i)*fsd(j)*fcor(i,j);
    }
  }
  //density::MVNORM_t<Type> neg_log_densityF(fvar);
  MVMIX_t<Type> neg_log_densityF(fvar,Type(conf.fracMixF));
  Eigen::LLT< Matrix<Type, Eigen::Dynamic, Eigen::Dynamic> > lltCovF(fvar);
  matrix<Type> LF = lltCovF.matrixL();
  matrix<Type> LinvF = LF.inverse();

  for(int i=1;i<timeSteps;i++){
    resF.col(i-1) = LinvF*(vector<Type>(logF.col(i)-logF.col(i-1)));    
    nll+=neg_log_densityF(logF.col(i)-logF.col(i-1)); // F-Process likelihood
    SIMULATE_F(of){
      if(conf.simFlag==0){
        logF.col(i)=logF.col(i-1)+neg_log_densityF.simulate();
      }
    }
  }

  if(CppAD::Variable(keep.sum())){ // add wide prior for first state, but _only_ when computing ooa residuals
    Type huge = 10;
    for (int i = 0; i < stateDimF; i++) nll -= dnorm(logF(i, 0), Type(0), huge, true);  
  } 

  if(conf.resFlag==1){
    ADREPORT_F(resF,of);
  }
  return nll;
}



template <class Type>
Type nllFseparable(confSet &conf, paraSet<Type> &par, array<Type> &logF, array<Type> &logW, data_indicator<vector<Type>,Type> &keep, objective_function<Type> *of){
  
  Type nll=0; 
  int stateDimF=logF.dim[0];
  int timeSteps=logF.dim[1];
  matrix<Type> SigmaU(stateDimF-1,stateDimF-1);
  SigmaU.setZero();
  vector<Type> sdU(stateDimF-1);  
  vector<Type> sdV(1);  
  for(int i=0; i<sdU.size(); ++i){
    sdU(i) = exp(par.sepFlogSd(0));
  }
  sdV(0) = exp(par.sepFlogSd(1));
  Type rhoU = trans(par.sepFlogitRho(0));
  Type rhoV = trans(par.sepFlogitRho(1));

  //Define logU and logV
  matrix<Type> logU(timeSteps,stateDimF-1);
  logU.setZero();
  vector<Type> logV(timeSteps);
  logV.setZero();
  for(int i=0; i<timeSteps; ++i){
    if(conf.corFlag ==3){
      logV(i)=(logF).col(i).mean();
    }else if(conf.corFlag ==4){
      logV(i)=(logF-logW).col(i).mean();
    }
  }
  for(int i=0; i<timeSteps; ++i){
    for(int j=0; j<stateDimF-1; ++j){
      if(conf.corFlag ==3){
        logU(i,j)=logF(j,i)-logV(i);
      }else if(conf.corFlag ==4){
        logU(i,j)=logF(j,i)-logW(j,i)-logV(i);
      }
    }
  }
  
  if(conf.corFlag ==4){
    //Define neg_log_densityW and add liklihood contribution from W
    int i;int j;
    vector<Type> sdW(stateDimF);
    matrix<Type> wvar(stateDimF,stateDimF);
    vector<Type> sdLogFsta=exp(par.logSdLogFsta);
    int stateDimN=conf.keyLogFsta.dim[1];
    for(i=0; i<stateDimF; ++i){
      for(j=0; j<stateDimN; ++j){
        if(conf.keyLogFsta(0,j)==i)break;
      }
      sdW(i)=sdLogFsta(conf.keyVarF(0,j));
    }
    matrix<Type> wcor(stateDimF,stateDimF);
    wcor.setZero();
    for(i=0; i<stateDimF; ++i){
      wcor(i,i)=1.0;
    }
    for(i=0; i<stateDimF; ++i){
      for(j=0; j<stateDimF; ++j){
        wvar(i,j)=sdW(i)*sdW(j)*wcor(i,j);
      }
    }
    MVMIX_t<Type> neg_log_densityW(wvar,Type(conf.fracMixF));
    
    for(i=0; i<timeSteps; ++i){
      nll+=neg_log_densityW(logW.col(i)); 
    }
  }

  //Likelihood contribution from U and V
  SigmaU.diagonal() = sdU*sdU;
  density::MVNORM_t<Type> nldens(SigmaU);
  for(int y=1; y<timeSteps; ++y){
    vector<Type> diff=vector<Type>(logU.row(y))-rhoU*vector<Type>(logU.row(y-1))- par.sepFalpha.segment(0,par.sepFalpha.size()-1);
    nll += nldens(diff);
  }
  for(int y=1; y<timeSteps; ++y){
    nll += -dnorm(logV(y),rhoV* logV(y-1) - par.sepFalpha(par.sepFalpha.size()-1) ,sdV(0),true);
  }
  
  //Accomondate for that logU and logV is a transformation of logF
  nll += -jacobiUVtrans(logF);

  return nll;
}


