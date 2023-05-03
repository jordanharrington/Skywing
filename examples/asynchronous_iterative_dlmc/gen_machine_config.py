import sys

def generate_machines():
    config = open("configuration.cfg", "w")
    for i in range(1, int(sys.argv[1]) + 1):
        config.writelines("machine{}\n".format(i))
        config.writelines("127.0.0.1\n")
        config.writelines("{}\n".format(1000 + ((i-1)* 100)))
        config.writelines("tag {}\n".format(i))
        config.writelines("-\n")
        for x in range(1, int(sys.argv[1]) + 1):
            if x != i:
                config.writelines("tag {}\n".format(x))
        config.writelines("-\n")
        if i < int(sys.argv[1]):
            config.writelines("machine{}\n".format(i+1))
        config.writelines("---\n")
    config.close()

generate_machines()