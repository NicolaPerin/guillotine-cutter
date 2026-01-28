pipeline {
    agent any
    
    stages {
        stage('Setup') {
            steps {
                echo 'Setting up Python environment...'
                sh '''
                    python3 -m venv venv
                    . venv/bin/activate
                    pip install --upgrade pip
                    pip install -e .
                    pip install pytest pytest-cov
                    pip list | grep -E "numpy|matplotlib|guillotine"
                '''
            }
        }
        
        stage('Lint') {
            steps {
                echo 'Running linters...'
                sh '''
                    . venv/bin/activate
                    pip install flake8
                    flake8 src/ --max-line-length=100 --exclude=__pycache__ || true
                '''
            }
        }
        
        stage('Test') {
            steps {
                echo 'Running tests...'
                sh '''
                    . venv/bin/activate
                    python -c "import numpy; print(f'NumPy: {numpy.__version__}')"
                    pytest tests/ -v --cov=guillotine --cov-report=xml --cov-report=term
                '''
            }
        }
        
        stage('Results') {
            steps {
                echo 'Test results generated'
            }
        }
    }
    
    post {
        always {
            echo 'Cleaning up...'
            sh 'rm -rf venv'
        }
        success {
            echo '✓ Build successful!'
        }
        failure {
            echo '✗ Build failed!'
        }
    }
}
