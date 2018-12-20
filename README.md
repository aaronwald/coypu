
# Coypu Websocket LV C++ Cache


```
git clone https://github.com/Tencent/rapidjson.git
```

Enable docker forwarding on host
```
sudo sysctl net.ipv4.conf.all.forwarding=1
sudo iptables -P FORWARD ACCEPT
```

```
docker system prune -a
docker build -t coypu .
docker run --dns 8.8.8.8 --rm -t coypu 
docker run --dns 8.8.8.8 --rm -i -t coypu --entrypoint /bin/bash
```
