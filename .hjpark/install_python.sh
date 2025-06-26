# 파이썬 설치
PYTHON_VERSION=3.11

apt update
apt install -y software-properties-common
add-apt-repository ppa:deadsnakes/ppa -y
apt update
apt install -y python${PYTHON_VERSION} python${PYTHON_VERSION}-dev python${PYTHON_VERSION}-venv
ln -sf $(which python${PYTHON_VERSION}) /usr/bin/python

apt update && apt install -y curl
curl -sS https://bootstrap.pypa.io/get-pip.py | python${PYTHON_VERSION}
python${PYTHON_VERSION} -m pip install --upgrade pip wheel
ln -sf $(which pip${PYTHON_VERSION}) /usr/bin/pip

echo -------------------------------------------------------------
python --version
pip --version
echo -------------------------------------------------------------
