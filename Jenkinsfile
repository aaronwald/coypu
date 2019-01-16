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
						  sh 'curl https://sh.rustup.rs -sSf | sh -s -- -y'
						  sh 'mkdir -p build'
						  sh '''
                  source $HOME/.cargo/env &&
                	export LD_LIBRARY_PATH=/usr/local/lib/ &&
                  mkdir libs &&
                  cd libs &&
                  git clone https://github.com/Tencent/rapidjson.git &&
                  cd .. &&
                  cd build &&
                	cmake -D CMAKE_BUILD_TYPE=Debug -D BUILD_TESTING=ON .. &&
                  make && 
                	make coyputest &&
                  ./coyputest --gtest_output="xml:testresults.xml"
                '''
					 }
				}
		  }
	 }
}
