
# Setup

```
virtualenv -p /bin/python3.6 test
source test/bin/activate
pip install --upgrade pip
pip install -r requirements.txt 
```
curl https://api-public.sandbox.pro.coinbase.com/products | python3 products.py 
