## Summary
- `Plinius` is a secure machine learning framework which leverages `Intel SGX` for secure training of neural network models, and `persistent memory` (PM) for fault tolerance.
- Plinius consists two main libradries: [sgx-romulus](https://github.com/anonymous-xh/sgx-romulus) which is an Intel SGX-compatible PM library we ported from Romulus PM library, and [sgx-dnet](https://github.com/anonymous-xh/sgx-dnet) which is a port of [Darknet](http://pjreddie.com/darknet) ML framework into Intel SGX.
- This readme gives a quick rundown on how to test secure training in Plinius as described in our paper.

## Training a model in Plinius
### Intro
- As described in the paper, training a model in Plinius is summarized in the workflow below:
![workflow](workflow.png)
- For the sake of simplicity we assume RA and SC have been done successfully and the encryption key has been provisioned to the enclave.i.e `enc_key` variable in [trainer.cpp](Enclave/train/trainer.cpp).
- You can find an encrypted version of the MNIST data set in: `App/dnet-out/data/mnist`.
- `enc_mnist_imgs.data` contains 60k encrypted mnist images and `enc_mnist_labels.data` contains 60k corresponding labels.
- The images and labels are encrypted with AES-GCM encryption algorithm, with a 16 byte MAC and 12 byte IV attached to each encrypted element (e.g. image or label).
- For clearer comprehension, the encrypted image and label files have the form below:
![enc_dataset](enc_dataset.png)
- We use a file on ramdisk i.e `/dev/shm/romoluslog_shared` to emulate PM. If you have a real PM device modify that path in the file: [Romulus_helper.h](App/Romulus_helper.h).

### Training the model
- As describe in the paper, we first initialize sgx-rom in the main routine via `rom_init` and `ecall_init` and invoke the `train_mnist` function.
- `train_mnist` reads the corresponding network/model configuration file and parses it into a config data structure and sends this to the enclave runtime via the `ecall_trainer` ecall.
- In the enclave we load the encrypted data once into PM and begin the training iterations. 
- For each iteration, the routine reads batches of encrypted data from PM, decrypts the former in the enclave, trains the model with the batch, and the mirrors-out weights to PM.
