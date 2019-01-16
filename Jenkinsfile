pipeline {
    agent {
		  docker {
				image 'gcr.io/massive-acrobat-227416/coypu_llvm'
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
