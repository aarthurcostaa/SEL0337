SEL0337 - Projetos em Sistemas Embarcados

Arthur Alves da Costa - 13751207

Raphael Franco - 13862393

Prática 5 - Checkpoint 1 e 2 - Atvidade extra na pasta yolo_raspicam

# SEL0337

Atividades desenvolvidas na disciplina SEL0337 – Projetos em Sistemas Embarcados.

## Prática 5 — systemd, Git e GitHub

No Checkpoint 1, um blink de LED em Python foi configurado como serviço do systemd para iniciar no boot da Raspberry Pi. No Checkpoint 2, os programas foram versionados com Git na Raspberry Pi e enviados a este repositório.

```
SEL0337/
├── .gitignore
├── README.md
└── pratica5/
    ├── blink.sh
    ├── blink_.py
    ├── blink_python.service
    └── blinkoff.sh
```

---

## Checkpoint 1 — Serviço de inicialização com o systemd

O systemd é o *init system* do Raspberry Pi OS: depois que o kernel é carregado, ele inicia os serviços descritos em *unit files* (`.service`). Os serviços são controlados com o `systemctl`.

### Programas

- **`blink.sh`**: blink em bash. Exporta a GPIO 15 em `/sys/class/gpio`, define o pino como saída e alterna o valor entre 1 e 0 a cada 0,2 s.
- **`blink_.py`**: blink em Python com a biblioteca gpiozero, executado pelo serviço. Ao receber SIGTERM (`systemctl stop`) ou SIGINT (Ctrl+C), sai do laço, apaga o LED e libera a GPIO.
- **`blink_python.service`**: unit file do serviço. O `ExecStart` executa o `blink_.py` com o `/usr/bin/python3`, o `ExecStop` chama o `blinkoff.sh`, o serviço roda com o usuário `sel` e é reiniciado em caso de falha. O `WantedBy=multi-user.target` faz o serviço iniciar no boot depois de habilitado.
- **`blinkoff.sh`**: executado no `ExecStop`. Registra no journal, com a etiqueta `pratica5-blink`, que a parada foi solicitada.

### Funcionamento do serviço

```bash
sudo cp blink_python.service /lib/systemd/system/
sudo systemctl daemon-reload            # relê os unit files
sudo systemctl start blink_python       # inicia o serviço
sudo systemctl enable blink_python      # inicia o serviço no boot
systemctl status blink_python           # estado do serviço
sudo systemctl stop blink_python        # para o serviço
journalctl -t pratica5-blink            # mensagem do blinkoff.sh
```
# Resultados 

Demonstração de funcionamento do Checkpoint 1

https://github.com/user-attachments/assets/20243970-f53e-46fd-a793-ad22999f24c3

---

## Checkpoint 2 — Git e GitHub

O repositório foi criado no GitHub e clonado na Raspberry Pi por HTTPS. As etapas registradas no terminal foram:

1. **Clone e inspeção:** `git clone`, `ls -la` (pasta `.git`), `git status` (árvore limpa) e `git remote -v` (remoto `origin`).
2. **`.gitignore`:** criado e versionado antes dos programas, para manter o `token_github.txt` fora do repositório.
3. **Commit dos programas:** `git add pratica5` e `git commit -m "checkpoint 1"`. O `token_github.txt` foi ignorado.
4. **Push:** `git push origin main`, usando o token de acesso pessoal no lugar da senha.
5. **Verificação:** `git log --oneline` mostrou `main` e `origin/main` no mesmo commit.

*Tabela 1 — Commits do repositório.*

| Commit | Mensagem | Conteúdo |
|---|---|---|
| `9823fd2` | Initial commit | README inicial |
| `39eae39` | Adicionando o git ignore | `.gitignore` |
| `08e2e74` | checkpoint 1 | programas da pasta `pratica5` |

O histórico completo está em `historico_git.txt`, gerado com `git log > historico_git.txt`.

<details>
<summary>Registro do terminal</summary>

```text
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5 $ git clone https://github.com/aarthurcostaa/SEL0337.git
Cloning into 'SEL0337'...
remote: Enumerating objects: 3, done.
remote: Counting objects: 100% (3/3), done.
remote: Compressing objects: 100% (2/2), done.
remote: Total 3 (delta 0), reused 0 (delta 0), pack-reused 0 (from 0)
Receiving objects: 100% (3/3), done.
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5 $ ls
blinkoff.sh  blink_.py  blink_python.service  blink.sh  SEL0337  token_github.txt
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5 $ cd SEL0337/
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ ls
README.md
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ ls -la
total 16
drwxr-xr-x 3 sel sel 4096 Nov 16 14:27 .
drwxr-xr-x 3 sel sel 4096 Nov 16 14:27 ..
drwxr-xr-x 8 sel sel 4096 Nov 16 14:27 .git
-rw-r--r-- 1 sel sel   81 Nov 16 14:27 README.md
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git status
On branch main
Your branch is up to date with 'origin/main'.

nothing to commit, working tree clean
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git remove -v
git: 'remove' is not a git command. See 'git --help'.

The most similar command is
	remote
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git remote -v
origin	https://github.com/aarthurcostaa/SEL0337.git (fetch)
origin	https://github.com/aarthurcostaa/SEL0337.git (push)
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ mkdir pratica5
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ ls
pratica5  README.md
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ nano .gitignore
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git status
On branch main
Your branch is up to date with 'origin/main'.

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	.gitignore
	pratica5/

nothing added to commit but untracked files present (use "git add" to track)
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git add .gitignore 
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git commit -m "Adicionando o git ignore"
[main 39eae39] Adicionando o git ignore
 1 file changed, 5 insertions(+)
 create mode 100644 .gitignore
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ ls
pratica5  README.md
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ cd pratica 5
bash: cd: too many arguments
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ cd pratica5
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ ls
blinkoff.sh  blink_.py  blink_python.service  blink.sh  token_github.txt
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ git add pratica5
fatal: pathspec 'pratica5' did not match any files
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ cd ..
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git add pratica5
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git commit -m "checkpoint 1"
[main 08e2e74] checkpoint 1
 4 files changed, 93 insertions(+)
 create mode 100755 pratica5/blink.sh
 create mode 100644 pratica5/blink_.py
 create mode 100644 pratica5/blink_python.service
 create mode 100755 pratica5/blinkoff.sh
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ git log --oneline
08e2e74 (HEAD -> main) checkpoint 1
39eae39 Adicionando o git ignore
9823fd2 (origin/main, origin/HEAD) Initial commit
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ ls
pratica5  README.md
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337 $ cd pratica5
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ ls
blinkoff.sh  blink_.py  blink_python.service  blink.sh  token_github.txt
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ cat token_github.txt

(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ git push origin main
Username for 'https://github.com': aarthuracostaa
Password for 'https://aarthuracostaa@github.com': 
Enumerating objects: 11, done.
Counting objects: 100% (11/11), done.
Delta compression using up to 4 threads
Compressing objects: 100% (9/9), done.
Writing objects: 100% (10/10), 1.75 KiB | 85.00 KiB/s, done.
Total 10 (delta 1), reused 0 (delta 0), pack-reused 0
remote: Resolving deltas: 100% (1/1), done.
To https://github.com/aarthurcostaa/SEL0337.git
   9823fd2..08e2e74  main -> main
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ git log --oneline
08e2e74 (HEAD -> main, origin/main, origin/HEAD) checkpoint 1
39eae39 Adicionando o git ignore
9823fd2 Initial commit
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ git commit -m "Checkpoint 2"
On branch main
Your branch is up to date with 'origin/main'.

nothing to commit, working tree clean
(0793) sel@raspberrypi:~/sel0337_arthur_franco/pratica5/SEL0337/pratica5 $ git log --oneline
08e2e74 (HEAD -> main, origin/main, origin/HEAD) checkpoint 1
39eae39 Adicionando o git ignore
9823fd2 Initial commit
```

</details>

