
    org 7C00H

    jmp short start
    times 22 db 0 ; Reserve for the filesystem data

start:
    ; Set to teletype print and clear direction
    mov ah, 0Eh
    cld

    ; Load the string relatively
    call get_ip
get_ip:
    pop si
    add si, (string - get_ip) 

print:
    lodsb
    test al, al
    jz short done
    int 10h
    jmp short print

done:

    ; Wait for key
    mov ax, 0
    int 16h

    ; Reboot
    mov al, 0FEh
    out 64h, al

    ; Safeguard
    jmp short $

string: db "This is not a bootable drive. Press any key to restart.", 0

    times 510-($-$$) db 0
    dw 0AA55H
