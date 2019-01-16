pipeline {
	 agent {
		  kubernetes {
				label 'mypod'
				defaultContainer 'jnlp'
				yamlFile 'pod.yaml'
		  }
	 }
	 stages {
		  container('coypu_llvm') {
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
}
