pipeline {
    agent {
		  docker {
				registryUrl 'https://gcr.io/massive-acrobat-227416/'
				registryCredentialsId 'gcr:coypu'
				image 'coypu_llvm:latest'
		  }
	 }
	 stages {
		  stage('Checkout') {
				steps {	 
					 checkout scm
					 sh 'mkdir -p build'
				}
		  }
		  stage('Compile') {
				steps {
					 sh '''
                	cd build &&
                	cmake -D CMAKE_BUILD_TYPE=Debug -D BUILD_TESTING=ON .. &&
                	make
                '''
				}
		  }
		  stage('Test') {
				steps {
					 sh '''
                	cd build &&
                	make test
                '''
				}
		  }
	 }
}
