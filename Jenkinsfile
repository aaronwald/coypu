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
				steps {
					 container('coypullvm') {
						  checkout scm
						  sh 'mkdir -p build'
						  sh '''
                	export LD_LIBRARY_PATH=/usr/local/lib/ &&
                  cd build &&
                	cmake -D CMAKE_BUILD_TYPE=Debug -D BUILD_TESTING=ON .. &&
                	make
                '''
						  sh '''
                	export LD_LIBRARY_PATH=/usr/local/lib/ &&
                 	cd build &&
                	make test
                '''
					 }
				}
		  }
	 }
}
