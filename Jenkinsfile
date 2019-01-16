pipeline {
	 agent {
		  kubernetes {
				label 'mypod'
				defaultContainer 'jnlp'
				yamlFile 'pod.yaml'
		  }
	 }
	 stages {
		  stage('Checkout') {
				container('coypu_llvm') {
					 steps {	 
						  checkout scm
						  sh 'mkdir -p build'
						  sh '''
                	cd build &&
                	cmake -D CMAKE_BUILD_TYPE=Debug -D BUILD_TESTING=ON .. &&
                	make
                '''
						  sh '''
                	cd build &&
                	make test
                '''
					 }
				}
		  }
	 }
}
